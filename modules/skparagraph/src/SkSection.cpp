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
    : fText(text), fParagraphStyle(style), fTextStyles(std::move(styles)),
      fUnbreakableWords(std::move(words)), fLines() {

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

// TODO: We can be smarter and check soft line breaks against glyph clusters (much later)
// Calculates words advances from runs
void SkSection::breakShapedTextIntoLinesByUnbreakableWords(SkScalar maxWidth,
                                                           size_t maxLines) {
  SkWords* wordsStart = fUnbreakableWords.begin();
  SkWords* words = fUnbreakableWords.begin();
  SkWords* lastWords = nullptr;
  SkRun* runStart = fRuns.begin();

  SkScalar wordsWidth = 0;
  SkScalar wordsHeight = 0;
  SkScalar wordsTrimmedWidth = 0;

  SkScalar lineWidth = 0;
  SkScalar lineHeight = 0;

  // This iterator will be called one more time in the end to add the last cluster
  SkRun::iterateThrough(SkArraySpan<SkRun>(fRuns, (size_t)0, fRuns.size()),
    [this,
     &lineWidth, &lineHeight,
     &wordsWidth, &wordsHeight, &wordsTrimmedWidth, maxWidth,
     &words, &lastWords, &wordsStart,
     &runStart]
    (SkCluster cluster) {

      // TODO: for now the cluster must not cross the words or be inside words entirely
      if (cluster.fCluster <= words->text()) {
        // Cluster is inside the word
        wordsWidth += cluster.fWidth;
        wordsHeight = SkTMax(wordsHeight, cluster.fHeight);
        return;
      } else {
        SkASSERT(!(cluster.fCluster && words->text()));
      }

      // Update the word
      words->setAdvance(wordsWidth, wordsHeight);
      words->setTrimmedWidth(wordsTrimmedWidth);

      // Update the line
      if (lineWidth + wordsTrimmedWidth > maxWidth) {
        // Trim the previous word
        if (lastWords != nullptr) {
          lineWidth -= lastWords->spaceWidth();
          //lastWords->trim();
        }
        // Add line
        fLines.emplace_back(
            lineWidth,
            lineHeight,
            SkArraySpan<SkWords>(fUnbreakableWords, wordsStart, words - wordsStart),
            SkArraySpan<SkRun>(fRuns, runStart, cluster.fRun - runStart));
        runStart = cluster.fRun;
        wordsStart = words;
        lineWidth = 0;
        lineHeight = 0;
      }

      // Add word
      lineWidth += words->width();
      lineHeight = SkTMax(lineHeight, words->height());
      fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, words->trimmedWidth());
      fMaxIntrinsicWidth = SkTMax(fMaxIntrinsicWidth, lineWidth);
      lastWords = words;

      // Move to the next word
      ++words;

      // Reset the counters
      wordsWidth = 0;
      wordsHeight = 0;
      wordsTrimmedWidth = 0;
    });
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

void SkSection::iterateThroughStyles(
    SkLine& line,
    SkStyleType styleType,
    std::function<void(SkSpan<const char> text, SkTextStyle style)> apply) {

  const char* start = fText.begin();
  size_t size = 0;
  SkTextStyle prevStyle;
  for (auto& textStyle : fTextStyles) {

    if (!(textStyle.text() && line.text())) {
      continue;
    }
    auto style = textStyle.style();
    auto text = textStyle.text();
    if (style.matchOneAttribute(styleType, prevStyle)) {
      size += text.size();
      continue;
    } else if (size == 0) {
      prevStyle = style;
      size = text.size();
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

    iterateThroughStyles(line, SkStyleType::Background,
        [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintBackground(textCanvas, text, style); } );

    iterateThroughStyles(line, SkStyleType::Shadow,
        [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintShadow(textCanvas, text, style); } );

    iterateThroughStyles(line, SkStyleType::Foreground,
        [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintText(textCanvas, text, style); } );

    iterateThroughStyles(line, SkStyleType::Decorations,
        [line, textCanvas](SkSpan<const char> text, SkTextStyle style) { line.paintDecorations(textCanvas, text, style); } );

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
