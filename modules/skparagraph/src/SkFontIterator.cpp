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
                               SkSpan<SkBlock>
                                       styles,
                               sk_sp<SkFontCollection>
                                       fonts,
                               bool hintingOn)
        : fText(utf8)
        , fStyles(styles)
        , fCurrentChar(utf8.begin())
        , fFontCollection(std::move(fonts))
        , fHintingOn(hintingOn) {
    findAllFontsForAllStyledBlocks();
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

void SkFontIterator::findAllFontsForAllStyledBlocks() {
    SkBlock combined;
    for (auto& block : fStyles) {
        SkASSERT(combined.text().begin() == nullptr ||
                 combined.text().end() == block.text().begin());

        if (combined.text().begin() != nullptr &&
            block.style().matchOneAttribute(SkStyleType::kFont, combined.style())) {
            combined.add(block.text());
            continue;
        }

        if (!combined.text().empty()) {
            findAllFontsForStyledBlock(combined.style(), combined.text());
        }

        combined = block;
    }
    findAllFontsForStyledBlock(combined.style(), combined.text());

    if (!fText.empty() && fFontMapping.find(fText.begin()) == nullptr) {
        // Resolve the first character with the first found font
        fFontMapping.set(fText.begin(), fFirstResolvedFont);
    }
}

void SkFontIterator::findAllFontsForStyledBlock(const SkTextStyle& style, SkSpan<const char> text) {
    fCodepoints.reset();
    fCharacters.reset();
    fUnresolvedIndexes.reset();
    fUnresolvedCodepoints.reset();

    // Extract all unicode codepoints
    const char* current = text.begin();
    while (current != text.end()) {
        fCharacters.emplace_back(current);
        fCodepoints.emplace_back(utf8_next(&current, text.end()));
        fUnresolvedIndexes.emplace_back(fUnresolvedIndexes.size());
    }
    fUnresolved = fCodepoints.size();

    // Walk through all available fonts to resolve the block
    for (auto& fontFamily : style.getFontFamilies()) {
        auto typeface = fFontCollection->matchTypeface(fontFamily, style.getFontStyle());
        if (typeface.get() == nullptr) {
            continue;
        }

        // Resolve all unresolved characters
        auto font = makeFont(typeface, style.getFontSize(), style.getHeight());
        resolveAllCharactersByFont(font);
        if (fUnresolved == 0) {
            break;
        }
    }

    addResolvedWhitespacesToMapping();

    if (fFontCollection->fontFallbackEnabled()) {
        while (fUnresolved > 0) {
            auto unicode = firstUnresolved();
            auto typeface = fFontCollection->defaultFallback(unicode, style.getFontStyle());
            if (typeface == nullptr) {
                break;
            }
            auto font = makeFont(typeface, style.getFontSize(), style.getHeight());
            if (!resolveAllCharactersByFont(font)) {
                // Not a single unicode character was resolved
                break;
            }
        }
    }

    // In case something still unresolved
    if (fResolvedFonts.count() == 0) {
        fFirstResolvedFont =
                makeFont(fFontCollection->defaultFallback(firstUnresolved(), style.getFontStyle()),
                         style.getFontSize(), style.getHeight());
    }
}

size_t SkFontIterator::resolveAllCharactersByFont(std::pair<SkFont, SkScalar> font) {
    // Consolidate all unresolved unicodes in one array to make a batch call
    SkTArray<SkGlyphID> glyphs(fUnresolved);
    glyphs.push_back_n(fUnresolved, SkGlyphID(0));
    font.first.getTypeface()->unicharsToGlyphs(
            fUnresolved == fCodepoints.size() ? fCodepoints.data() : fUnresolvedCodepoints.data(),
            fUnresolved, glyphs.data());

    SkRange<size_t> resolved(0, 0);
    SkRange<size_t> whitespaces(0, 0);
    size_t stillUnresolved = 0;

    auto processRuns = [&]() {
        if (resolved.width() == 0) {
            return;
        }

        if (resolved.width() == whitespaces.width()) {
            // The entire run is just whitespaces;
            // Remember the font and mark whitespaces back unresolved
            // to calculate its mapping for the other fonts
            for (auto w = whitespaces.start; w != whitespaces.end; ++w) {
                if (fWhitespaces.find(w) == nullptr) {
                    fWhitespaces.set(w, font);
                }
                fUnresolvedIndexes[stillUnresolved++] = w;
                fUnresolvedCodepoints.emplace_back(fCodepoints[w]);
            }
        } else {
            fFontMapping.set(fCharacters[resolved.start], font);
        }
    };

    // Try to resolve all the unresolved unicode points
    for (size_t i = 0; i < glyphs.size(); ++i) {
        auto glyph = glyphs[i];
        auto index = fUnresolvedIndexes[i];

        if (glyph == 0) {
            processRuns();

            resolved = SkRange<size_t>(0, 0);
            whitespaces = SkRange<size_t>(0, 0);

            fUnresolvedIndexes[stillUnresolved++] = index;
            fUnresolvedCodepoints.emplace_back(fCodepoints[index]);
            continue;
        }

        if (index == resolved.end) {
            ++resolved.end;
        } else {
            processRuns();
            resolved = SkRange<size_t>(index, index + 1);
        }
        if (u_isUWhiteSpace(fCodepoints[index])) {
            if (index == whitespaces.end) {
                ++whitespaces.end;
            } else {
                whitespaces = SkRange<size_t>(index, index + 1);
            }
        } else {
            whitespaces = SkRange<size_t>(0, 0);
        }
    }

    // One last time to take care of the tail run
    processRuns();

    size_t wasUnresolved = fUnresolved;
    fUnresolved = stillUnresolved;
    return fUnresolved < wasUnresolved;
}

void SkFontIterator::addResolvedWhitespacesToMapping() {
    size_t resolvedWhitespaces = 0;
    for (size_t i = 0; i < fUnresolved; ++i) {
        auto index = fUnresolvedIndexes[i];
        auto found = fWhitespaces.find(index);
        if (found != nullptr) {
            fFontMapping.set(fCharacters[index], *found);
            ++resolvedWhitespaces;
        }
    }
    fUnresolved -= resolvedWhitespaces;
}

std::pair<SkFont, SkScalar> SkFontIterator::makeFont(sk_sp<SkTypeface> typeface,
                                                     SkScalar size,
                                                     SkScalar height) {
    SkFont font(typeface, size);
    font.setEdging(SkFont::Edging::kAntiAlias);
    if (!fHintingOn) {
        font.setHinting(SkFontHinting::kSlight);
        font.setSubpixel(true);
    }
    auto pair = std::make_pair(font, height);

    auto foundFont = fResolvedFonts.find(pair);
    if (foundFont == nullptr) {
        if (fResolvedFonts.count() == 0) {
            fFirstResolvedFont = pair;
        }
        fResolvedFonts.add(pair);
    }

    return pair;
}
