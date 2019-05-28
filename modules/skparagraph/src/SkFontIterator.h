/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unicode/brkiter.h>
#include <unicode/ubidi.h>
#include "SkParagraphImpl.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPictureRecorder.h"
#include "src/core/SkSpan.h"
#include "src/utils/SkUTF.h"

class SkFontIterator final : public SkShaper::FontRunIterator {
public:
    SkFontIterator(SkSpan<const char> utf8,
                   SkSpan<SkBlock> styles,
                   sk_sp<SkFontCollection> fonts,
                   bool hintingOn);

    void consume() override;

    size_t endOfCurrentRun() const override { return fCurrentChar - fText.begin(); }
    bool atEnd() const override { return fCurrentChar == fText.end(); }
    const SkFont& currentFont() const override { return fFont; }
    SkScalar lineHeight() const { return fLineHeight; }

private:

    struct Hash {
        uint32_t operator()(const std::pair<SkFont, SkScalar>& key) const {
            return  SkTypeface::UniqueID(key.first.getTypeface()) +
                    SkScalarCeilToInt(key.first.getSize()) +
                    SkScalarCeilToInt(key.second);
        }
    };

    bool resolveFonts();

    bool resolveStyle(const SkTextStyle& style, const char* start, size_t size);

    std::pair<SkFont, SkScalar> makeFont(sk_sp<SkTypeface> typeface, SkScalar size,
                                         SkScalar height);

    SkSpan<const char> fText;
    const char* fCurrentChar;
    SkFont fFont;
    SkScalar fLineHeight;
    SkBlock* fCurrentStyle;
    SkSpan<SkBlock> fStyles;
    sk_sp<SkFontCollection> fFontCollection;
    SkTHashMap<const char*, std::pair<SkFont, SkScalar>> fFontMapping;
    SkTHashSet<std::pair<SkFont, SkScalar>, Hash> fResolvedFonts;
    bool fHintingOn;
    std::pair<SkFont, SkScalar> fFirstResolvedFont;
    const char* fFirstResolvedCharacter;
};