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

    fUnicodes.reserve(utf8.size());
    fCharacters.reserve(utf8.size());
    fUnresolvedUnicodes.reserve(utf8.size());
    fUnresolvedIndexes.reserve(utf8.size());
    resolveFonts();
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

bool SkFontIterator::resolveFonts() {
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

        resolved |= resolveStyle(prevStyle, SkSpan<const char>(start, size));

        // Start all over again
        prevStyle = style;
        size = block.text().size();
        start = block.text().begin();
    }

    resolved |= resolveStyle(prevStyle, SkSpan<const char>(start, size));

    if (fFontMapping.find(fText.begin()) == nullptr) {
        // Not all characters are resolved
        fFontMapping.set(start, fFirstResolvedFont);
    }

    return resolved;
}

size_t SkFontIterator::resolveByTypeface(std::pair<SkFont, SkScalar> font) {
    size_t stillUnresolved = 0;
    size_t wasUnresolved = fUnresolved;

    // Try to resolve all the unresolved unicodes at once
    SkTArray<SkGlyphID> glyphs(wasUnresolved);
    glyphs.push_back_n(wasUnresolved, SkGlyphID(0));
    font.first.getTypeface()->unicharsToGlyphs(fUnresolvedUnicodes.data(), wasUnresolved,
                                               glyphs.data());

    SkRange<size_t> resolvedRun(0, 0);
    SkRange<size_t> whitespaceRun(0, 0);
    for (size_t i = 0; i <= glyphs.size(); ++i) {
        auto glyph = i == glyphs.size() ? 0 : glyphs[i];
        if (glyph != 0) {
            auto index = fUnresolvedIndexes[i];
            auto uni = fUnicodes[index];
            if (index == resolvedRun.end) {
                // Continue with the run
                ++resolvedRun.end;
            } else {
                if (resolvedRun.width() == whitespaceRun.width()) {
                    // We need to "unresolve" all the whitespaces
                    for (auto index = whitespaceRun.start; index != whitespaceRun.end; ++index) {
                        if (fWhitespaces.find(index) == nullptr) {
                            fWhitespaces.set(index, font);
                        }
                        auto uni = fUnicodes[index];
                        fUnresolvedIndexes[stillUnresolved] = index;
                        fUnresolvedUnicodes[stillUnresolved] = uni;
                        ++stillUnresolved;
                    }
                } else {
                    // Nevermind whitespaces, just add remember the run
                    fFontMapping.set(fCharacters[resolvedRun.start], font);
                }
                // Start a new run
                resolvedRun = SkRange<size_t>(index, index + 1);
            }
            if (u_isUWhiteSpace(uni)) {
                if (index == whitespaceRun.end) {
                    // Continue with the run
                    ++whitespaceRun.end;
                } else {
                    // Start a new run
                    whitespaceRun = SkRange<size_t>(index, index + 1);
                }
            } else {
                // Cancel the whitespace run
                whitespaceRun = SkRange<size_t>(0, 0);
            }
        } else {
            // We have an extra glyph == 0 to take care of the last resolved run
            if (resolvedRun.width() > 0) {
                if (resolvedRun.width() == whitespaceRun.width()) {
                    // We need to "unresolve" all the whitespaces
                    for (auto index = whitespaceRun.start; index != whitespaceRun.end; ++index) {
                        if (fWhitespaces.find(index) == nullptr) {
                            fWhitespaces.set(index, font);
                        }
                        auto uni = fUnicodes[index];
                        fUnresolvedIndexes[stillUnresolved] = index;
                        fUnresolvedUnicodes[stillUnresolved] = uni;
                        ++stillUnresolved;
                    }
                } else {
                    // Nevermind whitespaces, just add remember the run
                    fFontMapping.set(fCharacters[resolvedRun.start], font);
                }
                resolvedRun = SkRange<size_t>(0, 0);
                whitespaceRun = SkRange<size_t>(0, 0);
            }

            if (i < glyphs.size()) {
                auto index = fUnresolvedIndexes[i];
                auto uni = fUnicodes[index];
                fUnresolvedIndexes[stillUnresolved] = index;
                fUnresolvedUnicodes[stillUnresolved] = uni;
                ++stillUnresolved;
            }
        }
    }
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
            fFontMapping.set(fCharacters[index], *found);
            ++resolvedWhitespaces;
        }
    }
    fUnresolved -= resolvedWhitespaces;
}

bool SkFontIterator::resolveStyle(const SkTextStyle& style, SkSpan<const char> text) {
    fUnicodes.reset();
    fCharacters.reset();
    fUnresolvedUnicodes.reset();
    fUnresolvedIndexes.reset();

    const char* current = text.begin();
    while (current != text.end()) {
        fCharacters.emplace_back(current);
        SkUnichar u = utf8_next(&current, text.end());
        fUnicodes.emplace_back(u);
    }
    fUnresolvedUnicodes = fUnicodes;
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
        resolveByTypeface(font);

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
            if (!resolveByTypeface(font)) {
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
