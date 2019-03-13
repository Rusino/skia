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
#include "SkParagraphStyle.h"
#include "SkShaper.h"
#include "SkSpan.h"
#include "SkTextStyle.h"
#include "SkTextBlobPriv.h"

#include "SkRun.h"
#include "SkLine.h"
#include "SkBlock.h"

class SkSection {
 public:

  SkSection(
      SkSpan<const char> text,
      const SkParagraphStyle& style,
      SkTArray<SkBlock, true> styles,
      SkTArray<SkWords, true> words);

  ~SkSection() = default;

  void shapeIntoLines(SkScalar maxWidth, size_t maxLines);

  void formatLinesByWords(SkScalar maxWidth);

  void paintEachLineByStyles(SkCanvas* textCanvas);

  SkScalar alphabeticBaseline() const { return fAlphabeticBaseline; }
  SkScalar height() const { return fHeight; }
  SkScalar width() const { return fWidth; }
  SkScalar ideographicBaseline() const { return fIdeographicBaseline; }
  SkScalar maxIntrinsicWidth() const { return fMaxIntrinsicWidth; }
  SkScalar minIntrinsicWidth() const { return fMinIntrinsicWidth; }

  void getRectsForRange(
      const char* start,
      const char* end,
      std::vector<SkTextBox>& result);

  inline size_t lineNumber() const { return fLines.size(); }

  SkSpan<SkBlock> selectStyles(SkSpan<const char> text);

 private:

  typedef SkShaper::RunHandler INHERITED;

  friend class ShapeHandler;

  void resetContext();

  bool shapeTextIntoEndlessLine();

  void mapRunsToWords();

  void breakShapedTextIntoLinesByUnbreakableWords(SkScalar maxWidth,
                                                  size_t maxLines);

  void shapeWordsIntoManyLines(SkWords* words, SkScalar width);

  void iterateThroughRuns(
      std::function<void(SkSpan<const char> text, SkRun& run)> apply
      );

  void iterateThroughStyles(
      SkLine& line,
      SkStyleType styleType,
      std::function<void(SkSpan<const char> text, SkTextStyle style)> apply);

  // Input
  SkSpan<const char> fText;
  SkParagraphStyle fParagraphStyle;
  SkTArray<SkBlock, true> fTextStyles;
  SkTArray<SkWords, true> fUnbreakableWords;

  // Output to Flutter
  SkScalar fAlphabeticBaseline;   // TODO: Not implemented yet
  SkScalar fIdeographicBaseline;  // TODO: Not implemented yet
  SkScalar fHeight;
  SkScalar fWidth;
  SkScalar fMaxIntrinsicWidth;
  SkScalar fMinIntrinsicWidth;

  // Internal structures
  SkTArray<SkRun, true> fRuns;
  SkTArray<SkLine, true> fLines;
  SkTArray<SkCluster, true> fClusters;
};

class MultipleFontRunIterator final : public FontRunIterator {
 public:
  MultipleFontRunIterator(
      SkSpan<const char> utf8,
      SkSpan<SkBlock> styles)
      : fText(utf8), fCurrent(utf8.begin()), fEnd(utf8.end()),
        fCurrentStyle(SkTextStyle()), fIterator(styles.begin()),
        fNext(styles.begin()), fLast(styles.end()) {

    fCurrentTypeface = SkTypeface::MakeDefault();
    MoveToNext();
  }

  void consume() override {

    if (fIterator == fLast) {
      fCurrent = fEnd;
    } else {
      fCurrent = fNext == fLast ? fEnd : std::next(fCurrent,
                                                   fNext->text().begin()
                                                       - fIterator->text().begin());
      fCurrentStyle = fIterator->style();
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
    auto nextTypeface = fNext->style().getTypeface();
    while (fNext != fLast && fNext->style().getTypeface() == nextTypeface) {
      ++fNext;
    }
  }

 private:
  SkSpan<const char> fText;
  const char* fCurrent;
  const char* fEnd;
  SkFont fFont;
  SkTextStyle fCurrentStyle;
  SkBlock* fIterator;
  SkBlock* fNext;
  SkBlock* fLast;
  sk_sp<SkTypeface> fCurrentTypeface;
};

class ShapeHandler final : public SkShaper::RunHandler {

 public:
  explicit ShapeHandler(SkSection& section)
      : fSection(&section)
      , fAdvance(SkVector::Make(0, 0))
      , fWordsToBreak(nullptr) {}

  explicit ShapeHandler(SkSection& section, SkWords* words)
      : fSection(&section)
      , fAdvance(SkVector::Make(0, 0))
      , fWordsToBreak(words) {}

  ~ShapeHandler() {
    if (fWordsToBreak != nullptr) {
      // Insert words coming from SkShaper into the list
      // (skipping the long word that has been broken into pieces)
      size_t left = fWordsToBreak - fSection->fUnbreakableWords.begin();
      size_t insert = fWordsProducedByShaper.size();
      size_t right = fSection->fUnbreakableWords.end() - fWordsToBreak;
      size_t total = left + right + insert;

      SkTArray<SkWords, true> bigger;
      bigger.reserve(total);

      bigger.move_back_n(left, fSection->fUnbreakableWords.begin());
      bigger.move_back_n(insert, fWordsProducedByShaper.begin());
      bigger.move_back_n(right, fSection->fUnbreakableWords.begin() + left + 1);

      fSection->fUnbreakableWords.swap(bigger);
    }
  }

  inline SkVector advance() const { return fAdvance; }

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

  void commitRun() override {}

  void commitRun1(SkVector advance) override {

    // TODO: this method is temp solution. SkShaper has to deal with it
    auto& run = fSection->fRuns.back();
    if (run.size() == 0) {
      fSection->fRuns.pop_back();
      return;
    }

    run.setWidth(advance.fX);
    fAdvance.fX += run.advance().fX;
    fAdvance.fY =
        SkMaxScalar(fAdvance.fY, run.descent() + run.leading() - run.ascent());
  }

  void commitLine() override {

    if (fWordsToBreak != nullptr) {
      // One run = one word
      auto& run = fSection->fRuns.back();
      fWordsProducedByShaper.emplace_back(run.text());
      auto& words = fWordsProducedByShaper.back();
      words.setStartRun(&run);
      words.setEndRun(&run);
      words.setAdvance(run.advance());
      words.setTrimmedWidth(run.advance().fX);
    } else {
      // Only one line is possible
    }
  }

  SkSection* fSection;
  SkVector fAdvance;
  SkWords* fWordsToBreak;
  SkTArray<SkWords> fWordsProducedByShaper;
};
