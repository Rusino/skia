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
        , fCurrentChar(utf8.begin())
        , fCurrentStyle(styles.begin())
        , fStyles(styles)
        , fFontCollection(std::move(fonts))
        , fHintingOn(hintingOn)
        , fFirstResolvedCharacter(nullptr) {
    resolveFonts();
}

void SkFontIterator::consume() {
    TRACE_EVENT0("skia", TRACE_FUNC);
    SkASSERT(fCurrentChar < fText.end());

    auto found = fFontMapping.find(fCurrentChar);
    if (found == nullptr) {
        SkASSERT(fCurrentChar == fText.begin());
        fFont = SkFont();
        fLineHeight = 1;
    } else {
        // Get the font
        fFont = found->first;
        fLineHeight = found->second;
    }

    // Find the first character (index) that cannot be resolved with the current font
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
    TRACE_EVENT0("skia", TRACE_FUNC);
    fFontMapping.reset();
    fResolvedFonts.reset();
    fFirstResolvedCharacter = nullptr;

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

        resolved |= resolveStyle(prevStyle, start, size);

        // Start all over again
        prevStyle = style;
        size = block.text().size();
        start = block.text().begin();
    }

    resolved |= resolveStyle(prevStyle, start, size);

    if (fFontMapping.find(fText.begin()) == nullptr) {
        // Not all characters are resolved
        fFontMapping.set(start, fFirstResolvedFont);
    }

    return resolved;
}

bool SkFontIterator::resolveStyle(const SkTextStyle& style, const char* start, size_t size) {
    TRACE_EVENT0("skia", TRACE_FUNC);

    // List of all unicodes
    SkTArray<SkUnichar> unicodes(size);
    SkTArray<const char*> characters;
    const char* current = start;
    const char* end = start + size;
    while (current != end) {
        characters.emplace_back(current);
        SkUnichar u = utf8_next(&current, end);
        unicodes.emplace_back(u);
    }

    // List of unicodes unresolved by all the fonts so far
    SkTArray<SkUnichar> unresolvedUnicodes = unicodes;
    // List of indexes of unresolved unicodes in the initial unicode list
    SkTArray<size_t> unresolvedIndexes(unicodes.size());
    for (size_t i = 0; i < unicodes.size(); ++i) {
        unresolvedIndexes.emplace_back(i);
    }
    SkTHashMap<size_t, std::pair<SkFont, SkScalar>> whitespaces;
    size_t unresolved = unicodes.size();

    auto resolveByTypeface = [&](sk_sp<SkTypeface> typeface, SkScalar fontSize,
                                 SkScalar lineHeight) {
        // Add font to the cache
        auto font = makeFont(typeface, fontSize, lineHeight);
        auto foundFont = fResolvedFonts.find(font);
        if (foundFont == nullptr) {
            if (fResolvedFonts.count() == 0) {
                fFirstResolvedFont = font;
            }
            fResolvedFonts.add(std::pair<SkFont, SkScalar>(font));
        }

        size_t stillUnresolved = 0;
        size_t wasUnresolved = unresolved;

        // Try to resolve all the unresolved unicodes at once
        SkTArray<SkGlyphID> glyphs(wasUnresolved);
        glyphs.push_back_n(wasUnresolved, SkGlyphID(0));
        typeface->unicharsToGlyphs(unresolvedUnicodes.data(), wasUnresolved, glyphs.data());

        SkRange<size_t> resolvedRun(0, 0);
        SkRange<size_t> whitespaceRun(0, 0);
        for (size_t i = 0; i <= glyphs.size(); ++i) {
            auto glyph = i == glyphs.size() ? 0 : glyphs[i];
            if (glyph != 0) {
                auto index = unresolvedIndexes[i];
                auto uni = unicodes[index];
                if (index == resolvedRun.end) {
                    // Continue with the run
                    ++resolvedRun.end;
                } else {
                    if (resolvedRun.width() == whitespaceRun.width()) {
                        // We need to "unresolve" all the whitespaces
                        for (auto index = whitespaceRun.start; index != whitespaceRun.end;
                             ++index) {
                            if (whitespaces.find(index) == nullptr) {
                                whitespaces.set(index, font);
                            }
                            auto uni = unicodes[index];
                            unresolvedIndexes[stillUnresolved] = index;
                            unresolvedUnicodes[stillUnresolved] = uni;
                            ++stillUnresolved;
                        }
                    } else {
                        // Nevermind whitespaces, just add remember the run
                        fFontMapping.set(characters[resolvedRun.start], font);
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
                        for (auto index = whitespaceRun.start; index != whitespaceRun.end;
                             ++index) {
                            if (whitespaces.find(index) == nullptr) {
                                whitespaces.set(index, font);
                            }
                            auto uni = unicodes[index];
                            unresolvedIndexes[stillUnresolved] = index;
                            unresolvedUnicodes[stillUnresolved] = uni;
                            ++stillUnresolved;
                        }
                    } else {
                        // Nevermind whitespaces, just add remember the run
                        fFontMapping.set(characters[resolvedRun.start], font);
                    }
                    resolvedRun = SkRange<size_t>(0, 0);
                    whitespaceRun = SkRange<size_t>(0, 0);
                }

                if (i < glyphs.size()) {
                    auto index = unresolvedIndexes[i];
                    auto uni = unicodes[index];
                    unresolvedIndexes[stillUnresolved] = index;
                    unresolvedUnicodes[stillUnresolved] = uni;
                    ++stillUnresolved;
                }
            }
        }
        unresolved = stillUnresolved;
        return stillUnresolved < wasUnresolved;
    };

    // Walk through all available fonts to resolve the block
    for (auto& fontFamily : style.getFontFamilies()) {
        auto typeface = fFontCollection->matchTypeface(fontFamily, style.getFontStyle());
        if (typeface.get() == nullptr) {
            continue;
        }
        resolveByTypeface(typeface, style.getFontSize(), style.getHeight());
        if (unresolved == 0) {
            break;
        }
    }

    // Remember all the whitespaces that still can look unresolved
    size_t resolvedWhitespaces = 0;
    for (size_t i = 0; i < unresolved; ++i) {
        auto index = unresolvedIndexes[i];
        auto found = whitespaces.find(index);
        if (found != nullptr) {
            auto ch = characters[index];
            fFontMapping.set(ch, *found);
            ++resolvedWhitespaces;
        }
    }
    unresolved -= resolvedWhitespaces;

    // In case something still unresolved
    if (fFontCollection->fontFallbackEnabled()) {
        while (unresolved > 0) {
            auto index = unresolvedIndexes[0];
            auto unicode = unresolvedUnicodes[index];
            auto typeface = fFontCollection->defaultFallback(unicode, style.getFontStyle());
            if (typeface == nullptr ||
                !resolveByTypeface(typeface, style.getFontSize(), style.getHeight())) {
                break;
            }
        }
    }

    if (fResolvedFonts.count() == 0) {
        fFirstResolvedFont =
                makeFont(fFontCollection->defaultFallback(unicodes[0], style.getFontStyle()),
                         style.getFontSize(), style.getHeight());
        fResolvedFonts.add(std::pair<SkFont, SkScalar>(fFirstResolvedFont));
    }
    return unresolved > 0;
}

std::pair<SkFont, SkScalar> SkFontIterator::makeFont(sk_sp<SkTypeface> typeface, SkScalar size,
                                                     SkScalar height) {
    TRACE_EVENT0("skia", TRACE_FUNC);
    SkFont font(typeface, size);
    font.setEdging(SkFont::Edging::kAntiAlias);
    if (!fHintingOn) {
        font.setHinting(SkFontHinting::kSlight);
        font.setSubpixel(true);
    }
    return std::make_pair(font, height);
}
