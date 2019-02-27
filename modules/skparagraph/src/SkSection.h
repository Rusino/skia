/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "uchar.h"
#include "SkColor.h"
#include "SkCanvas.h"
#include "SkFontMetrics.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "SkParagraphStyle.h"
#include "SkShaper.h"
#include "SkSpan.h"
#include "SkTextStyle.h"
#include "SkTextBlobPriv.h"

#include "SkRun.h"
#include "SkLine.h"
#include "SkWord.h"

class MultipleFontRunIterator final : public FontRunIterator {
  public:
    MultipleFontRunIterator(
        SkSpan<const char> utf8,
        SkSpan<StyledText> styles)
        : fText(utf8)
        , fCurrent(utf8.begin())
        , fEnd(utf8.end())
        , fCurrentStyle(SkTextStyle())
        , fIterator(styles.begin())
        , fNext(styles.begin())
        , fLast(styles.end()) {

        fCurrentTypeface = SkTypeface::MakeDefault();
        MoveToNext();
    }

    void consume() override {

        if (fIterator == fLast) {
            fCurrent = fEnd;
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
    StyledText* fIterator;
    StyledText* fNext;
    StyledText* fLast;
    sk_sp<SkTypeface> fCurrentTypeface;
};

class SkSection {
  public:

    SkSection(
        SkSpan<const char> text,
        const SkParagraphStyle& style,
        SkTArray<StyledText> styles,
        std::vector<SkWord> words);

    void shapeIntoLines(SkScalar maxWidth, size_t maxLines);

    void formatLinesByWords(SkScalar maxWidth);

    void paintEachLineByStyles(SkCanvas* textCanvas);

    SkScalar alphabeticBaseline() { return fAlphabeticBaseline; }
    SkScalar height() { return fHeight; }
    SkScalar width() { return fWidth; }
    SkScalar ideographicBaseline() { return fIdeographicBaseline; }
    SkScalar maxIntrinsicWidth() { return fMaxIntrinsicWidth; }
    SkScalar minIntrinsicWidth() { return fMinIntrinsicWidth; }

    void getRectsForRange(
        const char* start,
        const char* end,
        std::vector<SkTextBox>& result);

    size_t lineNumber() const { return fLines.size(); }

    static SkSpan<StyledText> selectStyles(SkSpan<const char> text, SkSpan<StyledText> styles);

  private:

    typedef SkShaper::RunHandler INHERITED;

    friend class ShapeHandler;

    bool shapeTextIntoEndlessLine();

    void mapRunsToWords();
    void breakEndlessLineIntoLinesByWords(SkScalar width, size_t maxLines);

    void shapeWordIntoManyLines(SkScalar width, const SkWord& word);

     // Input
    SkSpan<const char> fText;
    SkParagraphStyle fParagraphStyle;
    SkTArray<StyledText> fTextStyles;

    // Output to Flutter
    SkScalar fAlphabeticBaseline;   // TODO: Not implemented yet
    SkScalar fIdeographicBaseline;  // TODO: Not implemented yet
    SkScalar fHeight;
    SkScalar fWidth;
    SkScalar fMaxIntrinsicWidth;
    SkScalar fMinIntrinsicWidth;

    // Internal structures
    SkTArray<SkRun> fRuns;      // Shaped text, one line, broken into runs
    std::vector<SkWord> fWords; // Shaped text, one line, broken into words
    SkTArray<SkLine> fLines;    // Shaped text, broken into lines

    std::vector<SkWord>::iterator fWordInsertPosition;
};


class ShapeHandler final : public SkShaper::RunHandler {

  public:
    explicit ShapeHandler(SkSection* section, bool endlessLine)
    : fEndlessLine(endlessLine)
    , fSection(section)
    , fAdvance(SkVector::Make(0, 0)) { }

  private:
    // SkShaper::RunHandler interface
    SkShaper::RunHandler::Buffer newRunBuffer(
        const RunInfo& info,
        const SkFont& font,
        int glyphCount,
        SkSpan<const char> utf8) override {
      // Runs always go to the end of the list even if we insert words in the middle
        auto& run = fSection->fRuns.emplace_back(font, info, glyphCount, utf8);
        return run.newRunBuffer();
    }

    void commitRun() override {

      // TODO: recalculate run advance for glyphCount since SkShaped does not do it
        auto& run = fSection->fRuns.back();
        if (run.size() == 0) {
            fSection->fRuns.pop_back();
            return;
        }
        fAdvance.fX += run.advance().fX;
        fAdvance.fY = SkMaxScalar(fAdvance.fY, run.descent() + run.leading() - run.ascent());
    }

    void commitLine() override {

        if (!fEndlessLine) {
            // One run = one word
          auto& run = fSection->fRuns.back();
          fSection->fWordInsertPosition =
              fSection->fWords.emplace(fSection->fWordInsertPosition, run.text(), SkSpan<SkRun>(&run, 1)) + 1;
        } else {
            // Only one line is possible
        }
    }

    bool fEndlessLine;
    SkSection* fSection;
    SkVector fAdvance;
};
