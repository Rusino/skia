/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkSection.h"
#include "SkFontMetrics.h"
#include "SkWord.h"
#include "SkArraySpan.h"

template<typename T>
inline bool operator==(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.size() == b.size() && a.begin() == b.begin();
}

template<typename T>
inline bool operator<=(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.begin() >= b.begin() && a.end() <= b.end();
}

template<typename T>
inline bool operator&&(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.end() >= b.begin() && a.begin() <= b.end();
}

SkSection::SkSection(
    SkSpan<const char> text,
    const SkParagraphStyle& style,
    SkTArray<StyledText> styles,
    SkTArray<SkWord, true> words)
    : fText(text)
    , fParagraphStyle(style)
    , fTextStyles(std::move(styles))
    , fWords(std::move(words))
    , fLines() {

  fAlphabeticBaseline = 0;
  fIdeographicBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
}

bool SkSection::shapeTextIntoEndlessLine() {

  MultipleFontRunIterator font(fText, SkSpan<StyledText>(fTextStyles.data(), fTextStyles.size()));
  ShapeHandler handler(*this);
  SkShaper shaper(nullptr);
  shaper.shape(&handler,
               &font,
               fText.begin(),
               fText.size(),
               true,
               {0, 0},
               std::numeric_limits<SkScalar>::max());

  SkASSERT(fLines.empty());
  fMaxIntrinsicWidth = handler.advance().fX;
  return true;
}

void SkSection::mapWordsToRuns() {

  auto wordIter = fWords.begin();
  auto runIter = fRuns.begin();
  auto prevRunIter = runIter;

  while (wordIter != fWords.end() && runIter != fRuns.end()) {
    auto wordSpan = wordIter->span();
    auto runSpan = runIter->text();
    SkASSERT(wordSpan && runSpan);

    // Copy all the runs affecting the word
    wordIter->mapToRuns(SkArraySpan<SkRun>(fRuns,
                                           prevRunIter,
                                           runIter - prevRunIter + 1));

    // Move the iterator if we have to
    if (wordSpan.end() >= runSpan.end()) {
      ++runIter;
    }

    ++wordIter;
    prevRunIter = runIter;
  }
}

void SkSection::breakShapedTextIntoLinesByWords(SkScalar width, size_t maxLines) {

  SkDebugf("breakEndlessLineIntoLinesByWords\n");
  SkVector advance = SkVector::Make(0, 0);

  size_t lineBegin = 0;
  SkScalar baseline = 0;
  size_t wordGroupStart = 0;
  size_t wordGroupdEnd = 0;
  while (wordGroupStart != fWords.size()) {

    // Get together all words that cannot break line
    wordGroupdEnd = wordGroupStart;
    SkScalar wordsWidth = 0;
    SkScalar wordsHeight = 0;
    SkScalar trim = 0;
    while (wordGroupdEnd < fWords.size()) {
      auto& word = fWords[wordGroupdEnd];
      if (word.fMayLineBreakBefore && wordGroupStart != wordGroupdEnd) {
        break;
      }
      wordsHeight = SkMaxScalar(wordsHeight, word.fullAdvance().fY);
      wordsWidth += word.fullAdvance().fX;
      trim = word.fullAdvance().fX - word.trimmedAdvance().fX;
      baseline = SkMaxScalar(baseline, word.fBaseline);
      ++wordGroupdEnd;
    };

    auto& firstWord = fWords[wordGroupStart];
    auto& lastWord = fWords[wordGroupdEnd - 1];
    if (advance.fX + wordsWidth - trim > width) {
      // TODO: there is one limitation here - SkShaper starts breaking words from the new line

      if (advance.fX == 0 && !firstWord.fProducedByShaper) {
        // The word is too big!
        // Let SkShaper to break it into many words and insert these words instead of this big word
        // Then continue breaking as if nothing happened
        SkSpan<const char> text = SkSpan<const char>(
                                        firstWord.fText.begin(),
                                        lastWord.fText.end() - firstWord.fText.begin());
        // We insert new words after the group
        shapeWordsIntoManyLines(width, text, wordGroupStart, wordGroupdEnd);
        continue;
      } else {
        // Add the line and start counting again
        SkDebugf("break %d: %f + %f %f + %f ? %f\n",
                 wordGroupStart - lineBegin, fHeight, advance.fY, wordsWidth, advance.fX, width);
        fLines.emplace_back(advance, baseline, SkArraySpan<SkWord>(fWords, lineBegin, wordGroupStart));
        fWidth = SkMaxScalar(fWidth, advance.fX);
        fHeight += advance.fY;
        // Start the new line
        lineBegin = wordGroupStart;
        advance = SkVector::Make(0, 0);
        baseline = 0;
      }
    }

    // Check if the word only fits without spaces (that would be the last word on the line)
    bool lastWordOnTheLine = wordGroupdEnd == fWords.size() ||
                              advance.fX + wordsWidth > width;
    if (lastWordOnTheLine) lastWord.trim();

    // Keep counting words
    advance.fX += lastWordOnTheLine ? wordsWidth - trim : wordsWidth;
    advance.fY = SkMaxScalar(advance.fY, wordsHeight);
    fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, wordsWidth - trim);

    if (fLines.size() > maxLines) {
      // TODO: ellipsis
      break;
    }

    // Go to the next group of words
    wordGroupStart = wordGroupdEnd;
  }

  // Do not forget the rest of the words
  SkDebugf("break %d: %f + %f %f + %f ? %f\n",
           fWords.size() - lineBegin,
           fHeight,
           advance.fY,
           fWords.back().fullAdvance().fX,
           advance.fX,
           width);
  fLines.emplace_back(advance, baseline, SkArraySpan<SkWord>(fWords, lineBegin, fWords.size()));
  fWidth = SkMaxScalar(fWidth, advance.fX);
  fHeight += advance.fY;
}

SkSpan<StyledText> SkSection::selectStyles(SkSpan<const char> text, SkSpan<StyledText> styles) {

  auto start = styles.begin();
  while (start != styles.end() && start->fText.end() <= text.begin()) {
    ++start;
  }
  auto end = start;
  while (end != styles.end() && end->fText.begin() < text.end()) {
    ++end;
  }

  return SkSpan<StyledText>(start, end - start);
}

void SkSection::shapeWordsIntoManyLines(
    SkScalar width,
    SkSpan<const char> text,
    size_t groupStart,
    size_t groupEnd) {

  auto styles = selectStyles(text, SkSpan<StyledText>(fTextStyles.data(), fTextStyles.size()));
  MultipleFontRunIterator font(text, styles);
  SkShaper shaper(nullptr);
  ShapeHandler handler(*this, groupStart, groupEnd);

  shaper.shape(&handler,
               &font,
               text.begin(),
               text.size(),
               true,
               {0, 0},
      // TODO: Can we be more specific with max line number?
               width);
}

void SkSection::shapeIntoLines(SkScalar maxWidth, size_t maxLines) {

  fLines.reset();
  fRuns.reset();
  fAlphabeticBaseline = 0;
  fIdeographicBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;

  if (fWords.empty()) {
    // The section contains whitespaces and controls only
    SkASSERT(!fTextStyles.empty());
    SkFontMetrics metrics;
    fTextStyles.begin()->fStyle.getFontMetrics(&metrics);

    fWidth = 0;
    fHeight += metrics.fDescent + metrics.fLeading - metrics.fAscent;

    return;
  }

  shapeTextIntoEndlessLine();

  mapWordsToRuns();

  breakShapedTextIntoLinesByWords(maxWidth, maxLines);
}

void SkSection::formatLinesByWords(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  for (auto& line : fLines) {

    if (effectiveAlign == SkTextAlign::justify && &line == &fLines.back()) {
      effectiveAlign = SkTextAlign::left;
    }
    line.formatByWords(effectiveAlign, maxWidth);
    fWidth = SkMaxScalar(fWidth, line.advance().fX);
  }
}

void SkSection::paintEachLineByStyles(SkCanvas* textCanvas) {

  for (auto& line : fLines) {
    line.paintByStyles(textCanvas, SkSpan<StyledText>(fTextStyles.begin(), fTextStyles.size()));
    textCanvas->translate(0, line.advance().fY);
  }
}

void SkSection::getRectsForRange(
    const char* start,
    const char* end,
    std::vector<SkTextBox>& result) {

  for (auto& line : fLines) {
    line.getRectsForRange(fParagraphStyle.getTextDirection(),
                          start,
                          end,
                          result);
  }
}