/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkFontIterator.h"
#include <unicode/brkiter.h>
#include <unicode/ubidi.h>
#include "SkParagraphImpl.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPictureRecorder.h"
#include "src/core/SkSpan.h"
#include "src/utils/SkUTF.h"

namespace {
inline SkUnichar utf8_next(const char** ptr, const char* end) {
    SkUnichar val = SkUTF::NextUTF8(ptr, end);
    return val < 0 ? 0xFFFD : val;
}
}  // namespace

SkFontIterator::SkFontIterator(SkSpan<const char> utf8,
                               SkSpan<SkBlock> styles,
                               sk_sp<SkFontCollection> fonts,
                               bool hintingOn)
        : fText(utf8)
        , fCurrentChar(utf8.begin())
        , fCurrentStyle(styles.begin())
        , fStyles(styles)
        , fFontCollection(std::move(fonts))
        , fHintingOn(hintingOn) {

    mapAllUnicodesToFonts();
}

void SkFontIterator::consume() {
    SkASSERT(fCurrentChar < fText.end());
    auto found = fFontMapping.find(fCurrentChar);
    SkASSERT(found != nullptr);
    fFont = found->first;
    fLineHeight = found->second;

    // Move until we find the first character that cannot be resolved with the current font
    while (++fCurrentChar != fText.end()) {
        found = fFontMapping.find(fCurrentChar);
        if (found != nullptr) {
            if (fFont == found->first && fLineHeight == found->second) {
                continue;
            }
            break;
        }
    }
}

bool SkFontIterator::mapAllUnicodesToFonts() {
    // Walk through all the blocks
    auto resolved = true;
    const char* start = nullptr;
    size_t size = 0;
    SkTextStyle prevStyle;
    for (auto& block : fStyles) {
        auto style = block.style();
        if (start != nullptr && style.matchOneAttribute(SkStyleType::Font, prevStyle)) {
            size += block.text().size();
            continue;
        } else if (size == 0) {
            // First time only
            prevStyle = style;
            size = block.text().size();
            start = block.text().begin();
            continue;
        }

        resolved |= mapStyledBlockToFonts(prevStyle, SkSpan<const char>(start, size));

        // Start all over again
        prevStyle = style;
        size = block.text().size();
        start = block.text().begin();
    }

    resolved |= mapStyledBlockToFonts(prevStyle, SkSpan<const char>(start, size));

    if (fFontMapping.find(fText.begin()) == nullptr) {
        // Not all characters are resolved
        fFontMapping.set(start, fFirstResolvedFont);
    }

    return resolved;
}

size_t SkFontIterator::findAllCoveredUnicodesByFont(std::pair<SkFont, SkScalar> font) {
    size_t stillUnresolved = 0;

    // Consolidate all unresolved unicodes in one array to make a batch call
    SkTArray<SkGlyphID> glyphs(fUnresolved);
    glyphs.push_back_n(fUnresolved, SkGlyphID(0));
    if (fUnresolved == fUnicodes.size()) {
        font.first.getTypeface()->unicharsToGlyphs(fUnicodes.data(), fUnresolved, glyphs.data());
    } else {
        SkTArray<SkUnichar> unresolved(fUnresolved);
        for (size_t i = 0; i < fUnresolved; ++i) {
            unresolved.emplace_back(fUnicodes[fUnresolvedIndexes[i]]);
        }
        font.first.getTypeface()->unicharsToGlyphs(unresolved.data(), fUnresolved, glyphs.data());
    }

    // Try to resolve all the unresolved unicodes at once
    SkRange<size_t> resolvedRun(0, 0);
    SkRange<size_t> whitespaceRun(0, 0);

    auto processRuns = [&]() {
      if (resolvedRun.width() == whitespaceRun.width()) {
          // The entire run is just whitespaces; unresolve it
          for (auto w = whitespaceRun.start; w != whitespaceRun.end; ++w) {
              if (fWhitespaces.find(w) == nullptr) {
                  fWhitespaces.set(w, font);
              }
              fUnresolvedIndexes[stillUnresolved] = w;
              ++stillUnresolved;
          }
      } else {
          fFontMapping.set(fCodepoints[resolvedRun.start], font);
      }
    };

    for (size_t i = 0; i <= glyphs.size(); ++i) {
        auto glyph = i == glyphs.size() ? 0 : glyphs[i];
        if (glyph != 0) {
            auto index = fUnresolvedIndexes[i];
            if (index == resolvedRun.end) {
                ++resolvedRun.end;
            } else {
                processRuns();
                resolvedRun = SkRange<size_t>(index, index + 1);
            }
            if (u_isUWhiteSpace(fUnicodes[index])) {
                if (index == whitespaceRun.end) {
                    ++whitespaceRun.end;
                } else {
                    whitespaceRun = SkRange<size_t>(index, index + 1);
                }
            } else {
                whitespaceRun = SkRange<size_t>(0, 0);
            }
        } else {
            // We have an extra glyph == 0 to take care of the last resolved run
            if (resolvedRun.width() > 0) {
                processRuns();
                resolvedRun = SkRange<size_t>(0, 0);
                whitespaceRun = SkRange<size_t>(0, 0);
            }

            if (i < glyphs.size()) {
                fUnresolvedIndexes[stillUnresolved] = fUnresolvedIndexes[i];
                ++stillUnresolved;
            }
        }
    }

    size_t wasUnresolved = fUnresolved;
    fUnresolved = stillUnresolved;
    return stillUnresolved < wasUnresolved;
}

void SkFontIterator::addWhitespacesToResolved() {
    // Remember all the whitespaces that still can look unresolved
    size_t resolvedWhitespaces = 0;
    for (size_t i = 0; i < fUnresolved; ++i) {
        auto index = fUnresolvedIndexes[i];
        auto found = fWhitespaces.find(index);
        if (found != nullptr) {
            fFontMapping.set(fCodepoints[index], *found);
            ++resolvedWhitespaces;
        }
    }
    fUnresolved -= resolvedWhitespaces;
}

bool SkFontIterator::mapStyledBlockToFonts(const SkTextStyle& style, SkSpan<const char> text) {

    fUnicodes.reset();
    fCodepoints.reset();
    fUnresolvedIndexes.reset();

    fUnicodes.reserve(text.size());
    fCodepoints.reserve(text.size());
    fUnresolvedIndexes.reserve(text.size());

    const char* current = text.begin();
    while (current != text.end()) {
        fCodepoints.emplace_back(current);
        SkUnichar u = utf8_next(&current, text.end());
        fUnicodes.emplace_back(u);
    }
    for (size_t i = 0; i < fUnicodes.size(); ++i) {
        fUnresolvedIndexes.emplace_back(i);
    }
    fUnresolved = fUnicodes.size();

    // Walk through all available fonts to resolve the block
    for (auto& fontFamily : style.getFontFamilies()) {
        auto typeface = fFontCollection->matchTypeface(fontFamily, style.getFontStyle());
        if (typeface.get() == nullptr) {
            continue;
        }

        // Add font to the cache
        auto font = makeFont(typeface, style.getFontSize(), style.getHeight());
        auto foundFont = fResolvedFonts.find(font);
        if (foundFont == nullptr) {
            if (fResolvedFonts.count() == 0) {
                fFirstResolvedFont = font;
            }
            fResolvedFonts.add(std::pair<SkFont, SkScalar>(font));
        }

        // Resolve all unresolved characters
        findAllCoveredUnicodesByFont(font);

        if (fUnresolved == 0) {
            break;
        }
    }

    addWhitespacesToResolved();

    if (fFontCollection->fontFallbackEnabled()) {
        while (fUnresolved > 0) {
            auto unicode = firstUnresolved();
            auto typeface = fFontCollection->defaultFallback(unicode, style.getFontStyle());
            if (typeface == nullptr) {
                break;
            }
            auto font = makeFont(typeface, style.getFontSize(), style.getHeight());
            if (!findAllCoveredUnicodesByFont(font)) {
                break;
            }
        }
    }

    // In case something still unresolved
    if (fResolvedFonts.count() == 0) {
        fFirstResolvedFont =
                makeFont(fFontCollection->defaultFallback(firstUnresolved(), style.getFontStyle()),
                         style.getFontSize(), style.getHeight());
        fResolvedFonts.add(std::pair<SkFont, SkScalar>(fFirstResolvedFont));
    }

    return fUnresolved > 0;
}

std::pair<SkFont, SkScalar> SkFontIterator::makeFont(sk_sp<SkTypeface> typeface, SkScalar size,
                                                     SkScalar height) {
    SkFont font(typeface, size);
    font.setEdging(SkFont::Edging::kAntiAlias);
    if (!fHintingOn) {
        font.setHinting(SkFontHinting::kSlight);
        font.setSubpixel(true);
    }
    return std::make_pair(font, height);
}
