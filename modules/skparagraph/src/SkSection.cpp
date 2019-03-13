/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include "SkSection.h"
#include "SkFontMetrics.h"
#include "SkBlock.h"
#include <unicode/brkiter.h>

SkSection::SkSection(
    SkSpan<const char> text,
    const SkParagraphStyle& style,
    SkTArray<SkBlock, true> styles,
    SkTArray<SkWords, true> words)
    : fText(text)
    , fParagraphStyle(style)
    , fTextStyles(std::move(styles))
    , fUnbreakableWords(std::move(words))
    , fLines() {

  fAlphabeticBaseline = 0;
  fIdeographicBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
}

bool SkSection::shapeTextIntoEndlessLine() {

  MultipleFontRunIterator font(fText, SkSpan<SkBlock>(fTextStyles.data(), fTextStyles.size()));
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

std::string toString(SkSpan<const char> text) {
  icu::UnicodeString utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
  std::string str;
  utf16.toUTF8String(str);
  return str;
}

void SkSection::mapRunsToWords() {

  SkRun* run = fRuns.begin();
  SkWords* words = fUnbreakableWords.begin();

  SkScalar wordsWidth = 0;
  SkScalar trimmedWidth = 0;
  while (run != fRuns.end()) {
    SkASSERT(words != fUnbreakableWords.end());
    run->iterateThrough([this, run, &words, &wordsWidth, &trimmedWidth](SkCluster cluster) {
      bool once = true;
      while (words != fUnbreakableWords.end()) {
        words->setEndRun(run);
        if (words->text() && cluster.fText) {
          // TODO: There is a big assumption that words made of whole clusters;
          SkASSERT(cluster.fText <= words->text());
          wordsWidth += cluster.fWidth;
          trimmedWidth += cluster.fWidth;
          if (words->getStartRun() == nullptr) {
            words->setStartRun(run);
          }
          break;
        } else if (words->trimmed() && cluster.fText) {
          SkASSERT(cluster.fText <= words->trimmed());
          wordsWidth += cluster.fWidth;
          if (words->getStartRun() == nullptr) {
            words->setStartRun(run);
          }
          break;
        } else {
          SkASSERT(once);
          words->setTrimmedWidth(trimmedWidth);
          words->setAdvance(wordsWidth, SkTMax(words->height(), cluster.fHeight));
          once = false;
          wordsWidth = 0;
          trimmedWidth = 0;
          ++words;
        }
      }
    });
    ++run;
  }
}

void SkSection::breakShapedTextIntoLinesByUnbreakableWords(SkScalar maxWidth,
                                                           size_t maxLines) {
  SkWords* wordsStart = fUnbreakableWords.begin();
  SkWords* words = fUnbreakableWords.begin();
  SkWords* lastWords = nullptr;

  SkScalar lineWidth = 0;
  SkScalar lineHeight = 0;
  for (size_t i = 0; i < fUnbreakableWords.size(); ++i) {

    words = &fUnbreakableWords[i];
    auto wordsWidth = words->width();
    auto wordsTrimmedWidth = words->trimmedWidth();

    if (lineWidth + wordsTrimmedWidth > maxWidth) {
      if (lastWords == nullptr) {
        // This is the beginning of the line; the word is too long. Aaaa!!!
        shapeWordsIntoManyLines(words, maxWidth);
        continue;
      }

      // Trim the last word on the line
      lineWidth -= lastWords->spaceWidth();
      lastWords->trim();

      // Add one more line
      fLines.emplace_back(
          lineWidth,
          lineHeight,
          SkArraySpan<SkWords>(fUnbreakableWords, wordsStart, words - wordsStart),
          SkArraySpan<SkRun>(fRuns, wordsStart->getStartRun(), lastWords->getEndRun() - wordsStart->getStartRun() + 1));
      wordsStart = words;
      lineWidth = 0;
      lineHeight = 0;
    }

    // Add word to the line
    lineWidth += wordsWidth;
    lineHeight = SkTMax(lineHeight, words->height());
    fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, wordsTrimmedWidth);
    fMaxIntrinsicWidth = SkTMax(fMaxIntrinsicWidth, lineWidth);
    lastWords = words;
  }

  // Last hanging line
  fLines.emplace_back(
      lineWidth,
      lineHeight,
      SkArraySpan<SkWords>(fUnbreakableWords, wordsStart, words - wordsStart),
      SkArraySpan<SkRun>(fRuns, wordsStart->getStartRun(), lastWords->getEndRun() - wordsStart->getStartRun() + 1));
}

void SkSection::shapeWordsIntoManyLines(SkWords* words, SkScalar width) {

  if (words->isProducedByShaper()) {
    // TODO: What should be do in this case? We cannot call Shaper second time
    SkASSERT(false);
    return;
  }

  auto text(words->text());
  MultipleFontRunIterator font(text, selectStyles(text));
  SkShaper shaper(nullptr);
  ShapeHandler handler(*this, words);

  shaper.shape(&handler,
               &font,
               text.begin(),
               text.size(),
               true,
               {0, 0},
               width);
}

void SkSection::resetContext() {
  fLines.reset();
  fRuns.reset();

  fAlphabeticBaseline = 0;
  fIdeographicBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
}

void SkSection::shapeIntoLines(SkScalar maxWidth, size_t maxLines) {

  resetContext();

  if (fUnbreakableWords.empty()) {
    // The section contains whitespaces and controls only
    SkASSERT(!fTextStyles.empty());
    SkFontMetrics metrics;
    fTextStyles.begin()->style().getFontMetrics(&metrics);
    fWidth = 0;
    fHeight += metrics.fDescent + metrics.fLeading - metrics.fAscent;
    return;
  }

  shapeTextIntoEndlessLine();

  mapRunsToWords();

  breakShapedTextIntoLinesByUnbreakableWords(maxWidth, maxLines);
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

void SkSection::iterateThroughRuns(
    std::function<void(SkSpan<const char> text, SkRun& run)> apply) {

}

SkSpan<SkBlock> SkSection::selectStyles(SkSpan<const char> text) {

  auto start = fTextStyles.begin();
  while (start != fTextStyles.end() && start->text().end() <= text.begin()) {
    ++start;
  }
  auto end = start;
  while (end != fTextStyles.end() && end->text().begin() < text.end()) {
    ++end;
  }

  return SkSpan<SkBlock>(start, end - start);
}

// TODO: fix it
void SkSection::iterateThroughStyles(
    SkLine& line,
    SkStyleType styleType,
    std::function<void(SkSpan<const char> text, SkTextStyle style)> apply) {

  const char* start = nullptr;
  size_t size = 0;
  SkTextStyle prevStyle;
  for (auto& textStyle : fTextStyles) {

    if (!(textStyle.text() && line.text())) {
      continue;
    }
    auto style = textStyle.style();
    auto begin = SkTMax(textStyle.text().begin(), line.text().begin());
    auto end = SkTMin(textStyle.text().end(), line.text().end());
    auto text = SkSpan<const char>(begin, end - begin);
    if (style.matchOneAttribute(styleType, prevStyle)) {
      size += text.size();
      continue;
    } else if (size == 0) {
      // First time only
      prevStyle = style;
      size = text.size();
      start = text.begin();
      continue;
    }
    // Get all the words that cross this span
    // Generate a text blob
    apply(SkSpan<const char>(start, size), prevStyle);
    // Start all over again
    prevStyle = style;
    start = text.begin();
    size = text.size();
  }

  apply(SkSpan<const char>(start, size), prevStyle);
}

// TODO: Is it correct to paint ALL the section in this sequence?
// TODO: Optimize drawing
void SkSection::paintEachLineByStyles(SkCanvas* textCanvas) {

  for (auto& line : fLines) {

    textCanvas->save();
    textCanvas->translate(line.fShift - line.fOffset.fX, - line.fOffset.fY);

    //iterateThroughStyles(line, SkStyleType::Background,
    //    [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintBackground(textCanvas, text, style); } );

    //iterateThroughStyles(line, SkStyleType::Shadow,
    //    [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintShadow(textCanvas, text, style); } );

    iterateThroughStyles(line, SkStyleType::Foreground,
        [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintText(textCanvas, text, style); } );

    //iterateThroughStyles(line, SkStyleType::Decorations,
    //    [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintDecorations(textCanvas, text, style); } );

    textCanvas->restore();
  }
}

void SkSection::getRectsForRange(
    const char* start,
    const char* end,
    std::vector<SkTextBox>& result) {

  for (auto words = fUnbreakableWords.begin(); words != fUnbreakableWords.end(); ++words) {
    if (words->text().end() <= start || words->text().begin() >= end) {
      continue;
    }
    words->getRectsForRange(fParagraphStyle.getTextDirection(), start, end, result);
  }
}
