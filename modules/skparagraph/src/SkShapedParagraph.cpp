/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkShapedParagraph.h"
#include "SkFontMetrics.h"

SkShapedParagraph::SkShapedParagraph(SkParagraphStyle style,
    std::vector<StyledText> styles)
    : fParagraphStyle(style), fTextStyles(std::move(styles)) {

    fAlphabeticBaseline = 0;
    fIdeographicBaseline = 0;
    fHeight = 0;
    fWidth = 0;
    fMaxIntrinsicWidth = 0;
    fMinIntrinsicWidth = 0;
    _exceededLimits = false;

    fLines.emplace_back();
}

void SkShapedParagraph::layout(SkScalar maxWidth, size_t maxLines) {

    class MultipleFontRunIterator final : public FontRunIterator {
      public:
        MultipleFontRunIterator(SkSpan<const char> utf8,
            std::vector<StyledText>::iterator begin,
            std::vector<StyledText>::iterator end,
            SkTextStyle defaultStyle)
            : fText(utf8), fCurrent(utf8.begin()), fEnd(utf8.end()), fCurrentStyle(
            SkTextStyle()), fDefaultStyle(defaultStyle), fIterator(begin), fNext(
            begin), fLast(end) {

            fCurrentTypeface = SkTypeface::MakeDefault();
            MoveToNext();
        }

        void consume() override {

            if (fIterator == fLast) {
                fCurrent = fEnd;
                fCurrentStyle = fDefaultStyle;
            } else {
                fCurrent = fNext == fLast ? fEnd : std::next(fCurrent,
                                                             fNext->fText.begin()
                                                                 - fIterator->fText.begin());
                fCurrentStyle = fIterator->fStyle;
            }

            fCurrentTypeface = fCurrentStyle.getTypeface();
            fFont = SkFont(fCurrentTypeface, fCurrentStyle.getFontSize());

            MoveToNext();
        }
        const char* endOfCurrentRun() const override {

            return fCurrent;
        }
        bool atEnd() const override {

            return fCurrent == fEnd;
        }

        const SkFont* currentFont() const override {

            return &fFont;
        }

        void MoveToNext() {

            fIterator = fNext;
            if (fIterator == fLast) {
                return;
            }
            auto nextTypeface = fNext->fStyle.getTypeface();
            while (fNext != fLast && fNext->fStyle.getTypeface() == nextTypeface) {
                ++fNext;
            }
        }

      private:
        SkSpan<const char> fText;
        const char* fCurrent;
        const char* fEnd;
        SkFont fFont;
        SkTextStyle fCurrentStyle;
        SkTextStyle fDefaultStyle;
        std::vector<StyledText>::iterator fIterator;
        std::vector<StyledText>::iterator fNext;
        std::vector<StyledText>::iterator fLast;
        sk_sp<SkTypeface> fCurrentTypeface;
    };

    fMaxLines = maxLines;

    if (!fTextStyles.empty()) {
        auto start = fTextStyles.begin()->fText.begin();
        auto end =
            fTextStyles.empty() ? start - 1
                                : std::prev(fTextStyles.end())->fText.end();
        if (start < end) {
            SkSpan<const char> run(start, end - start);
            MultipleFontRunIterator font(run,
                                         fTextStyles.begin(),
                                         fTextStyles.end(),
                                         fParagraphStyle.getTextStyle());
            SkShaper shaper(nullptr);
            shaper.shape(this,
                         &font,
                         start,
                         end - start,
                         true,
                         {0, 0},
                         maxWidth);

            if (fLines.end()->words().empty()) {
                fLines.pop_back();
            }
            return;
        }

        // Shaper does not shape empty lines
        SkFontMetrics metrics;
        fTextStyles.back().fStyle.getFontMetrics(metrics);
        fAlphabeticBaseline = -metrics.fAscent;
        fIdeographicBaseline = -metrics.fAscent;
        fHeight = metrics.fDescent + metrics.fLeading - metrics.fAscent;
        fWidth = 0;
        fMaxIntrinsicWidth = 0;
        fMinIntrinsicWidth = 0;
        return;
    }

    // Shaper does not shape empty lines
    fHeight = 0;
    fWidth = 0;
    fMaxIntrinsicWidth = 0;
    fMinIntrinsicWidth = 0;
}

void SkShapedParagraph::printBlocks(size_t linenum) {

    SkDebugf("Paragraph #%d\n", linenum);
    if (!fTextStyles.empty()) {
        SkDebugf("Lost blocks\n");
        for (auto& block : fTextStyles) {
            std::string str(block.fText.begin(), block.fText.size());
            SkDebugf("Block: '%s'\n", str.c_str());
        }
    }
    int i = 0;
    for (auto& line : fLines) {
        SkDebugf("Line: %d (%d)\n", i, line.words().size());
        for (auto& word : line.words()) {
            std::string str(word.text().begin(), word.text().size());
            SkDebugf("Block: '%s'\n", str.c_str());
        }
        ++i;
    }
}

void SkShapedParagraph::format(SkScalar maxWidth) {

    size_t lineIndex = 0;
    for (auto& line : fLines) {

        ++lineIndex;
        SkScalar delta = maxWidth - line.advance().fX;
        if (delta <= 0) {
            // Delta can be < 0 if there are extra whitespaces at the end of the line;
            // This is a limitation of a current version
            continue;
        }

        switch (fParagraphStyle.effective_align()) {
            case SkTextAlign::left:break;
            case SkTextAlign::right:
                for (auto& word : line.words()) {
                    word.shift(delta);
                }
                line.advance().fX = maxWidth;
                fWidth = maxWidth;
                break;
            case SkTextAlign::center: {
                auto half = delta / 2;
                for (auto& word : line.words()) {
                    word.shift(half);
                }
                line.advance().fX = maxWidth;
                fWidth = maxWidth;
                break;
            }
            case SkTextAlign::justify: {
                if (&line == &fLines.back()) {
                    break;
                }
                if (line.words().size() == 1) {
                    break;
                }
                SkScalar step = delta / (line.words().size() - 1);
                SkScalar shift = 0;
                for (auto& word : line.words()) {
                    word.shift(shift);
                    if (&word != &line.words().back()) {
                        word.expand(step);
                        line.advance().fX = maxWidth;
                        fWidth = maxWidth;
                    }
                    shift += step;
                }
                break;
            }
            default:break;
        }
    }
}

// TODO: currently we pick the first style of the run and go with it regardless
void SkShapedParagraph::paint(SkCanvas* textCanvas) {

    auto styleBegin = fTextStyles.begin();
    for (auto& line : fLines) {
        for (auto word : line.words()) {

            // Find the first style that affects the run
            while (styleBegin != fTextStyles.end()
                && styleBegin->fText.begin() < word.text().begin()) {
                ++styleBegin;
            }

            auto styleEnd = styleBegin;
            while (styleEnd != fTextStyles.end()
                && styleEnd->fText.begin() < word.text().end()) {
                ++styleEnd;
            }

            word.Paint(textCanvas, styleBegin, styleEnd);
        }
    }
}

void SkShapedParagraph::GetRectsForRange(
    const char* start,
    const char* end,
    std::vector<SkTextBox>& result) {

    for (auto& line : fLines) {
        for (auto& word : line.words()) {
            if (word.text().end() <= start || word.text().begin() >= end) {
                continue;
            }
            result.emplace_back(word.rect(), fParagraphStyle.getTextDirection());
        }
    }
}