/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkSection.h"
#include "SkFontMetrics.h"
#include "SkWord.h"

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
    std::vector<StyledText> styles,
    std::vector<SkSpan<const char>> softLineBreaks)
    : fText(text)
    , fParagraphStyle(style)
    , fTextStyles(std::move(styles))
    , fSoftLineBreaks(std::move(softLineBreaks)) {

  fAlphabeticBaseline = 0;
  fIdeographicBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
}

bool SkSection::shapeTextIntoEndlessLine() {

  if (fTextStyles.empty()) return false;

  auto start = fTextStyles.begin()->fText.begin();
  auto end = fTextStyles.empty() ? start - 1
                                 : std::prev(fTextStyles.end())->fText.end();
  if (start >= end) return false;

  SkSpan<const char> run(start, end - start);
  MultipleFontRunIterator font(run, SkSpan<StyledText>(fTextStyles));
  ShapeHandler handler(this, true /* entire text on one line */);
  SkShaper shaper(nullptr);
  shaper.shape(&handler,
               &font,
               start,
               end - start,
               true,
               {0, 0},
               std::numeric_limits<SkScalar>::max());

  SkASSERT(fLines.empty());
  SkDebugf("shapeTextIntoEndlessLine (%d)\n", this->fRuns.size());
  return true;
}


void SkSection::breakEndlessLineIntoWords() {

  SkDebugf("breakEndlessLineIntoWords\n");

  auto wordIter = fSoftLineBreaks.begin();
  auto runIter = fRuns.begin();
  auto prevRunIter = runIter;

  while (wordIter != fSoftLineBreaks.end() && runIter != fRuns.end()) {
    auto wordSpan = *wordIter;
    auto runSpan = runIter->text();
    SkASSERT(wordSpan && runSpan);

    // Copy all the runs affecting the word
    fWords.emplace_back(wordSpan, SkSpan<SkRun>(prevRunIter, runIter - prevRunIter + 1));

    // Move the iterator if we have to
    if (wordSpan.end() >= runSpan.end()) {
      ++runIter;
    }

    ++wordIter;
    prevRunIter = runIter;
  }
}

// This is the trickiest part: we need to break/merge shaper buffers
// Actually, the tricky part is hidden inside SkWord constructor
void SkSection::breakEndlessLineIntoLinesByWords(SkScalar width, size_t maxLines) {

  SkDebugf("breakEndlessLineIntoLinesByWords\n");
  SkVector advance = SkVector::Make(0, 0);
  auto lineBegin = &fWords.front();

  for (auto& word : fWords) {

    if (advance.fX + word.trimmedAdvance().fX > width) {
      if (advance.fX == 0) {
        // The word is too big! Remove it and ask shaper to break it
        fWords.pop_back();
        // Re-shape this word with the given width and use all the breaks from the shaper
        shapeWordIntoManyLines(width, word);
        // The last line may not be full; we are going to break it again
        auto last = fLines.back();
        advance = last.advance();
        fLines.pop_back();
      } else {
        // Add the line and start counting again
        SkDebugf("break %d: %f + %f ? %f\n",
                 &word - lineBegin, word.fullAdvance().fX, advance.fX, width);
        fLines.emplace_back(advance, SkSpan<SkWord>(lineBegin, &word - lineBegin));
        fWidth = SkMaxScalar(fWidth, advance.fX);
        fHeight += advance.fY;
        // Start the new line
        lineBegin = &word;
        advance = SkVector::Make(0, 0);
      }
    }

    // Check if the word only fits without spaces (that would be the last word on the line)
    bool lastWordOnTheLine = advance.fX + word.fullAdvance().fX > width;

    // Keep counting words
    advance.fX += lastWordOnTheLine ? word.trimmedAdvance().fX : word.fullAdvance().fX;
    advance.fY = SkMaxScalar(advance.fY, word.fullAdvance().fY);

    if (fLines.size() > maxLines) {
      // TODO: ellipsis
      break;
    }
  }

  // Do not forget the rest of the words
  SkDebugf("break %d: %f + %f ? %f\n",
           fWords.end() - lineBegin,
           fWords.back().fullAdvance().fX,
           advance.fX,
           width);
  fLines.emplace_back(advance, SkSpan<SkWord>(lineBegin, fWords.end() - lineBegin));
  fWidth = SkMaxScalar(fWidth, advance.fX);
  fHeight += advance.fY;
}

void SkSection::shapeWordIntoManyLines(SkScalar width, SkWord& word) {

  SkDebugf("shapeWordIntoManyLines\n");
  auto start = word.fStyles.begin()->fText.begin();
  auto end = word.fStyles.empty() ? start - 1
                                  : std::prev(word.fStyles.end())->fText.end();

  SkSpan<const char> run(start, end - start);
  MultipleFontRunIterator font(run, word.fStyles);
  SkShaper shaper(nullptr);
  ShapeHandler handler(this, false /* one words many lines */);
  fLines.emplace_back();
  shaper.shape(&handler,
               &font,
               start,
               end - start,
               true,
               {0, 0},
      // TODO: Can we be more specific with max line number?
               width);
}

void SkSection::shapeIntoLines(SkScalar maxWidth, size_t maxLines) {

  // Get rid of all the "empty text" cases
  if (fTextStyles.empty()) {
    // Shaper does not shape empty lines
    fHeight = 0;
    fWidth = 0;
    fMaxIntrinsicWidth = 0;
    fMinIntrinsicWidth = 0;
    return;
  }

  auto start = fTextStyles.begin()->fText.begin();
  auto end = fTextStyles.empty() ? start - 1
                                 : std::prev(fTextStyles.end())->fText.end();
  if (start == end) {
    // Shaper does not shape empty lines
    SkFontMetrics metrics;
    fTextStyles.begin()->fStyle.getFontMetrics(&metrics);
    fAlphabeticBaseline = -metrics.fAscent;
    fIdeographicBaseline = -metrics.fAscent;
    fHeight = metrics.fDescent + metrics.fLeading - metrics.fAscent;
    fWidth = 0;
    fMaxIntrinsicWidth = 0;
    fMinIntrinsicWidth = 0;
    return;
  }

  shapeTextIntoEndlessLine();

  breakEndlessLineIntoWords();

  breakEndlessLineIntoLinesByWords(maxWidth, maxLines);
}

void SkSection::formatLinesByWords(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  for (auto& line : fLines) {

    if (effectiveAlign == SkTextAlign::justify && &line == &fLines.back()) {
      effectiveAlign = SkTextAlign::left;
    }
    line.formatByWords(effectiveAlign, maxWidth);
  }
}

void SkSection::paintEachLineByStyles(SkCanvas* textCanvas) {

  SkScalar offset = 0;
  for (auto& line : fLines) {
    line.paintByStyles(textCanvas, offset, SkSpan<StyledText>(fTextStyles));
    offset += line.advance().fY;
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