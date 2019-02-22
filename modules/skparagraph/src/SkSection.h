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
#include "SkShapedRun.h"
#include "SkShapedLine.h"

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

class SkSection final : SkShaper::RunHandler {
  public:

    SkSection(SkParagraphStyle style, std::vector<StyledText> styles, std::vector<SkSpan<const char>> softBreaks);

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

  private:

    typedef SkShaper::RunHandler INHERITED;

    bool shapeTextIntoEndlessLine();

    void breakEndlessLineIntoWords();

    void breakEndlessLineIntoLinesByWords(SkScalar width);

    void shapeWordIntoManyLines(SkScalar width, SkWord& word);

    // SkShaper::RunHandler interface
    SkShaper::RunHandler::Buffer newRunBuffer(
        const RunInfo& info,
        const SkFont& font,
        int glyphCount,
        SkSpan<const char> utf8) override {

        SkASSERT(fStatus != ShapingNothing);

        if (fStatus == ShapingOneLine) {
            auto& run = fRuns.emplace_back(font, info, glyphCount, utf8);
            return run.newRunBuffer();
        } else {
            fRun = SkRun(font, info, glyphCount, utf8);
            return fRun.newRunBuffer();
        }
    }

    void commitRun() override {

        SkASSERT(fStatus != ShapingNothing);
        if (fStatus == ShapingOneLine) {
            auto& run = fRuns.back();
            if (run.size() == 0) {
                fRuns.pop_back();
            }
        } else if (fStatus == ShapingOneWord) {
            if (fRuns.size() != 0) {
                fWords.emplace_back(fRun.text(), fRun);
            }
        }
    }

    void commitLine() override {
        SkASSERT(fStatus != ShapingNothing);
        if (fStatus == ShapingOneLine) {
            // We have only one line
            SkASSERT(false);
        } else {
            fLines.emplace_back();
        }
    }

    // Constrains
    size_t fMaxLines;

    // Input
    SkParagraphStyle fParagraphStyle;
    std::vector<StyledText> fTextStyles;
    std::vector<SkSpan<const char>> fSoftLineBreaks;
    SkSpan<const char> fText;

    // Output to Flutter
    SkScalar fAlphabeticBaseline;   // TODO: Not implemented yet
    SkScalar fIdeographicBaseline;  // TODO: Not implemented yet
    SkScalar fHeight;
    SkScalar fWidth;
    SkScalar fMaxIntrinsicWidth;
    SkScalar fMinIntrinsicWidth;

    // Internal structures
    SkTArray<SkRun> fRuns;   // Shaped text, one line, broken into runs
    SkTArray<SkWord> fWords; // Shaped text, one line, broken into words
    SkTArray<SkLine> fLines; // Shaped text, broken into lines
    enum ShapingStatus {
        ShapingNothing,
        ShapingOneLine,
        ShapingOneWord
    };

    ShapingStatus fStatus;
    SkRun fRun;
};