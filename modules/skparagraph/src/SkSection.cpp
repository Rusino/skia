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
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"

SkSection::SkSection(
    SkSpan<const char> text,
    const SkParagraphStyle& style,
    SkTArray<SkBlock, true> styles,
    SkTArray<SkWords, true> unbreakableWords)
    : fText(text)
    , fParagraphStyle(style)
    , fTextStyles(std::move(styles))
    , fUnbreakableWords(std::move(unbreakableWords))
    , fLines() {
}

std::string toString(SkSpan<const char> text) {
  icu::UnicodeString utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
  std::string str;
  utf16.toUTF8String(str);
  return str;
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

void SkSection::buildClusterTable() {

  for (size_t runIndex = 0; runIndex < fRuns.size(); ++runIndex) {
    auto& run = fRuns[runIndex];
    size_t cluster = 0;
    SkScalar width = 0;
    size_t start = 0;
    SkDebugf("Cluster: %d\n", run.size());
    for (size_t pos = 0; pos <= run.size(); ++pos) {

      auto next = pos == run.size() ? run.fText.size() : run.fClusters[pos];
      if (pos == run.size()) {
        width += run.fInfo.fAdvance.fX - run.fPositions[start].fX + run.fPositions[0].fX;
      } else if (pos > 0) {
        width += run.fPositions[pos].fX - run.fPositions[start].fX;
      }
      if (cluster == next) {
        // Many glyphs in one cluster
        continue;
      } else if (next > cluster + 1) {
        // Many characters in one cluster
      }
      SkCluster data;
      data.fRunIndex = runIndex;
      data.fStart = start;
      data.fEnd = pos;
      data.fText = SkSpan<const char>(run.fText.begin() + cluster, next - cluster);
      data.fWidth = width;
      data.fHeight = run.calculateHeight();
      fClusters.emplace_back(data);
      SkDebugf("%d@%d-%d: %f '%s'\n", runIndex, start, pos, width,  toString(data.fText).c_str());

      cluster = next;
      start = pos;
      width = 0;
    }
  }
}

void SkSection::breakShapedTextIntoLinesByUnbreakableWords(SkScalar maxWidth,
                                                           size_t maxLines) {
  SkWords* wordsStart = fUnbreakableWords.begin();
  SkWords* words = fUnbreakableWords.begin();
  SkWords* lastWords = nullptr;

  SkVector lineAdvance = SkVector::Make(0, 0);
  SkVector lineOffset = SkVector::Make(0, 0);
  for (size_t i = 0; i < fUnbreakableWords.size(); ++i) {

    words = &fUnbreakableWords[i];
    measureWords(*words);

    auto wordsWidth = words->width();
    auto wordsTrimmedWidth = words->trimmedWidth();

    SkDebugf("Word: %f + %f: '%s'\n", lineAdvance.fX, wordsWidth, toString(words->full()).c_str());

    if (lineAdvance.fX + wordsTrimmedWidth > maxWidth) {
      if (lineAdvance.fX == 0) {
        // This is the beginning of the line; the word is too long. Aaaa!!!
        shapeWordsIntoManyLines(words, maxWidth);
        continue;
      }

      // Trim the last word on the line
      lineAdvance.fX -= lastWords->spaceWidth();
      lastWords->trim();

      // Add one more line
      fLines.emplace_back(
          lineOffset,
          lineAdvance,
          SkArraySpan<SkWords>(fUnbreakableWords, wordsStart, words - wordsStart));
      wordsStart = words;
      lastWords = words;
      auto& line = fLines.back();
      SkDebugf("Line #%d @%f,%f: '%s'\n", fLines.size(), line.fOffset.fX, line.fOffset.fY, toString(line.text()).c_str());

      lineOffset.fY += lineAdvance.fY;
      // Shift the rest of the line horizontally to the left
      // to compensate for the run positions since we broke the line
      lineOffset.fX = - findOffset(words->full().begin());
      lineAdvance = SkVector::Make(0, 0);
    }

    // Add word to the line
    lineAdvance.fX += wordsWidth;
    lineAdvance.fY = SkTMax(lineAdvance.fY, words->height());
    fMinIntrinsicWidth = SkTMax(fMinIntrinsicWidth, wordsTrimmedWidth);
    fMaxIntrinsicWidth = SkTMax(fMaxIntrinsicWidth, lineAdvance.fX);
    lastWords = words;
  }

  // Last hanging line
  lineAdvance.fX -= lastWords->spaceWidth();
  lastWords->trim();
  fLines.emplace_back(
      lineOffset,
      lineAdvance,
      SkArraySpan<SkWords>(fUnbreakableWords, wordsStart, fUnbreakableWords.end() - wordsStart));
  auto& line = fLines.back();
  SkDebugf("Line #%d @%f,%f: '%s'\n", fLines.size(), line.fOffset.fX, line.fOffset.fY, toString(line.text()).c_str());
}

void SkSection::shapeWordsIntoManyLines(SkWords* words, SkScalar width) {

  if (words->isProducedByShaper()) {
    // TODO: What should be do in this case? We cannot call Shaper second time
    SkASSERT(false);
    return;
  }

  auto text(words->full());
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
  fClusters.reset();

  fAlphabeticBaseline = 0;
  fIdeographicBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
}

// TODO: Find something better
size_t SkSection::findCluster(const char* ch) const {
  for (size_t i = 0; i < fClusters.size(); ++i) {
    auto& cluster = fClusters[i];
    if (cluster.fText.end() > ch) {
      SkASSERT(cluster.fText.begin() <= ch);
      return i;
    }
  }
  return fClusters.size();
}

SkScalar SkSection::findOffset(const char* ch) const {

  size_t index = findCluster(ch);
  SkASSERT(index < fClusters.size());

  auto cluster = fClusters[index];
  SkASSERT(cluster.fText.begin() == ch);

  auto run = fRuns[cluster.fRunIndex];
  return run.fPositions[cluster.fStart].fX;
}

SkVector SkSection::measureText(SkSpan<const char> text) const {

  SkVector size = SkVector::Make(0, 0);
  if (text.empty()) {
    return size;
  }

  auto start = findCluster(text.begin());
  auto end = findCluster(text.end() - 1);
  for (auto cl = start; cl <= end; ++cl) {

    SkASSERT(cl < fClusters.size());
    auto& cluster = fClusters[cl];

    if (cl == start) {
      size.fX -= cluster.sizeToChar(text.begin());
    }
    if (cl == end) {
      size.fX += cluster.sizeFromChar(text.end() - 1);
    } else {
      size.fX += cluster.fWidth;
    }
    size.fY = SkTMax(size.fY, cluster.fHeight);
  }

  return size;
}

void SkSection::measureWords(SkWords& words) const {

  auto full = measureText(words.full());
  auto trimmed = measureText(words.trimmed());

  words.setSizes(full, trimmed.fX);
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

  buildClusterTable();

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
    fHeight += line.advance().fY;
  }
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

void SkSection::iterateThroughStyles(
    SkSpan<const char> text,
    SkStyleType styleType,
    std::function<void(SkSpan<const char> text, SkTextStyle style)> apply) const {

  const char* start = nullptr;
  size_t size = 0;
  SkTextStyle prevStyle;
  for (auto& textStyle : fTextStyles) {

    if (!(textStyle.text() && text)) {
      continue;
    }
    auto style = textStyle.style();
    auto begin = SkTMax(textStyle.text().begin(), text.begin());
    auto end = SkTMin(textStyle.text().end(), text.end());
    auto intersect = SkSpan<const char>(begin, end - begin);
    if (style.matchOneAttribute(styleType, prevStyle)) {
      size += intersect.size();
      continue;
    } else if (size == 0) {
      // First time only
      prevStyle = style;
      size = intersect.size();
      start = intersect.begin();
      continue;
    }
    // Get all the words that cross this span
    // Generate a text blob
    apply(SkSpan<const char>(start, size), prevStyle);
    // Start all over again
    prevStyle = style;
    start = intersect.begin();
    size = intersect.size();
  }

  apply(SkSpan<const char>(start, size), prevStyle);
}

// TODO: Optimize the search
void SkSection::iterateThroughRuns(
    SkSpan<const char> text,
    std::function<void(const SkRun* run, size_t pos, size_t size, SkRect clip)> apply) const {

  auto start = findCluster(text.begin());
  auto end = findCluster(text.end() - 1);

  SkRect clip = SkRect::MakeEmpty();
  size_t size = 0;
  size_t pos = 0;

  const SkRun* run = nullptr;
  for (auto cl = start; cl <= end; ++cl) {
    auto& cluster = fClusters[cl];
    auto clusterRun = &fRuns[cluster.fRunIndex];

    if (run != clusterRun) {
      if (run != nullptr) {
        apply(run, pos, size, clip);
      }
      run = clusterRun;
      clip = SkRect::MakeXYWH(run->fInfo.fOffset.fX, run->fInfo.fOffset.fY, 0, 0);
      size = 0;
      pos = cluster.fStart;
    }

    size += (cluster.fEnd - cluster.fStart);
    if (cl == start) {
      clip.fLeft = clusterRun->fPositions[cluster.fStart].fX;
      clip.fRight = clip.fLeft;
      clip.fLeft += cluster.sizeToChar(text.begin());
    }
    if (cl == end) {
      clip.fRight += cluster.sizeFromChar(text.end() - 1);
    } else {
      clip.fRight += cluster.fWidth;
    }
    clip.fBottom = SkTMax(clip.fBottom, cluster.fHeight);
  }

  //clip.offset(run->offset());

  //SkDebugf("Clip: @%d,%d [%f:%f]\n", pos, size, clip.fLeft, clip.fRight);
  apply(run, pos, size, clip);
}

// TODO: Justification dropped again for now. It's really gets in a way!
void SkSection::paintText(
    SkCanvas* canvas,
    SkSpan<const char> text,
    SkTextStyle style) const {

  // Paint the blob with one foreground color
  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.reset();
    paint.setColor(style.getColor());
  }
  paint.setAntiAlias(true);

  // Build the blob from all the runs
  iterateThroughRuns(text,
     [canvas, paint](const SkRun* run, int32_t pos, size_t size, SkRect rect) {

       SkTextBlobBuilder builder;
       const auto& blobBuffer = builder.allocRunPos(run->fFont, SkToInt(size));
       sk_careful_memcpy(blobBuffer.glyphs,
                         run->fGlyphs.data() + pos,
                         size * sizeof(SkGlyphID));
       sk_careful_memcpy(blobBuffer.points(),
                         run->fPositions.data() + pos,
                         size * sizeof(SkPoint));

       auto blob = builder.make();
       canvas->save();
       canvas->clipRect(rect);
       canvas->drawTextBlob(blob, 0, 0, paint);
       canvas->restore();
     });
}

void SkSection::paintBackground(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const {

  if (!style.hasBackground()) {
    return;
  }

  iterateThroughRuns(
      text,
      [canvas, style](const SkRun* run, int32_t pos, size_t size, SkRect clip) {
        canvas->drawRect(clip, style.getBackground());
      });
}

void SkSection::paintShadow(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const {

  if (style.getShadowNumber() == 0) {
    return;
  }

  for (SkTextShadow shadow : style.getShadows()) {
    if (!shadow.hasShadow()) {
      continue;
    }

    SkPaint paint;
    paint.setColor(shadow.fColor);
    if (shadow.fBlurRadius != 0.0) {
      paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle,
                                                 shadow.fBlurRadius,
                                                 false));
    }

    iterateThroughRuns(text,
       [canvas, shadow, paint](const SkRun* run, int32_t pos, size_t size, SkRect rect) {
         SkTextBlobBuilder builder;
         const auto& blobBuffer = builder.allocRunPos(run->fFont, size);
         sk_careful_memcpy(blobBuffer.glyphs,
                           run->fGlyphs.data() + pos,
                           size * sizeof(SkGlyphID));
         sk_careful_memcpy(blobBuffer.points(),
                           run->fPositions.data() + pos,
                           size * sizeof(SkPoint));

         auto blob = builder.make();
         canvas->save();
         canvas->clipRect(rect.makeOffset(shadow.fOffset.x(), shadow.fOffset.y()));
         canvas->drawTextBlob(blob, shadow.fOffset.x(), shadow.fOffset.y(), paint);
         canvas->restore();
       });
  }
}

void SkSection::computeDecorationPaint(SkPaint& paint, SkRect clip, SkTextStyle textStyle, SkPath& path) const {

  paint.setStyle(SkPaint::kStroke_Style);
  if (textStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(textStyle.getColor());
  } else {
    paint.setColor(textStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);

  SkScalar scaleFactor = textStyle.getFontSize() / 14.f;

  switch (textStyle.getDecorationStyle()) {
    case SkTextDecorationStyle::kSolid:
      break;

    case SkTextDecorationStyle::kDouble:
      break;

      // Note: the intervals are scaled by the thickness of the line, so it is
      // possible to change spacing by changing the decoration_thickness
      // property of TextStyle.
    case SkTextDecorationStyle::kDotted: {
      const SkScalar intervals[] =
          {1.0f * scaleFactor, 1.5f * scaleFactor, 1.0f * scaleFactor,
           1.5f * scaleFactor};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, (int32_t) count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
      // Note: the intervals are scaled by the thickness of the line, so it is
      // possible to change spacing by changing the decoration_thickness
      // property of TextStyle.
    case SkTextDecorationStyle::kDashed: {
      const SkScalar intervals[] =
          {4.0f * scaleFactor, 2.0f * scaleFactor, 4.0f * scaleFactor,
           2.0f * scaleFactor};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, (int32_t) count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
    case SkTextDecorationStyle::kWavy: {

      int wave_count = 0;
      SkScalar x_start = 0;
      SkScalar wavelength = 2 * scaleFactor;
      auto width = clip.width();
      path.moveTo(0, 0);
      while (x_start + wavelength * 2 < width) {
        path.rQuadTo(wavelength,
                     wave_count % 2 != 0 ? wavelength : -wavelength,
                     wavelength * 2,
                     0);
        x_start += wavelength * 2;
        ++wave_count;
      }
      break;
    }
  }
}

// TODO: Make the thickness reasonable
void SkSection::paintDecorations(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle textStyle) const {

  if (textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  iterateThroughRuns(text,
      [this, canvas, textStyle](const SkRun* run, int32_t pos, size_t size, SkRect clip) {

     SkScalar thickness = textStyle.getDecorationThicknessMultiplier();
     SkScalar position;
     switch (textStyle.getDecoration()) {
       case SkTextDecoration::kUnderline:
         position = - run->fInfo.fAscent + thickness;
         break;
       case SkTextDecoration::kOverline:
         position = thickness;
         break;
       case SkTextDecoration::kLineThrough: {
         position = (- run->fInfo.fAscent - thickness) / 2;
         break;
       }
       default:
         position = 0;
         SkASSERT(false);
         break;
     }

     auto width = clip.width();
     SkScalar x = clip.left();
     SkScalar y = clip.top() + position;

     // Decoration paint (for now) and/or path
     SkPaint paint;
     SkPath path;
     this->computeDecorationPaint(paint, clip, textStyle, path);
     paint.setStrokeWidth(thickness);

     switch (textStyle.getDecorationStyle()) {
       case SkTextDecorationStyle::kWavy:
         path.offset(x, y);
         canvas->drawPath(path, paint);
         break;
       case SkTextDecorationStyle::kDouble: {
         canvas->drawLine(x, y, x + width, y, paint);
         SkScalar bottom = y + thickness * 2;
         canvas->drawLine(x, bottom, x + width, bottom, paint);
         break;
       }
       case SkTextDecorationStyle::kDashed:
       case SkTextDecorationStyle::kDotted:
       case SkTextDecorationStyle::kSolid:
         canvas->drawLine(x,
                          y,
                          x + width,
                          y,
                          paint);
         break;
       default:
         break;
     }
  });
}

// TODO: Is it correct to paint ALL the section in this sequence?
// TODO: Optimize drawing
void SkSection::paintEachLineByStyles(SkCanvas* textCanvas) {

  for (auto& line : fLines) {

    if (line.text().empty()) {
      continue;
    }

    textCanvas->save();
    textCanvas->translate(line.fShift + line.fOffset.fX, line.fOffset.fY);

    iterateThroughStyles( line.text(), SkStyleType::Background,
        [this, textCanvas](SkSpan<const char> text, SkTextStyle style) { this->paintBackground(textCanvas, text, style); } );

    iterateThroughStyles( line.text(),SkStyleType::Shadow,
        [this, textCanvas](SkSpan<const char> text, SkTextStyle style) { this->paintShadow(textCanvas, text, style); } );

    iterateThroughStyles(
        line.text(),
        SkStyleType::Foreground,
        [this, textCanvas](SkSpan<const char> text, SkTextStyle style) {
          this->paintText(textCanvas, text, style);
        });

    iterateThroughStyles( line.text(),SkStyleType::Decorations,
        [this, textCanvas](SkSpan<const char> text, SkTextStyle style) { this->paintDecorations(textCanvas, text, style); } );

    textCanvas->restore();
  }
}

void SkSection::getRectsForRange(
    const char* start,
    const char* end,
    std::vector<SkTextBox>& result) {

  for (auto words = fUnbreakableWords.begin(); words != fUnbreakableWords.end(); ++words) {
    if (words->full().end() <= start || words->full().begin() >= end) {
      continue;
    }
    words->getRectsForRange(fParagraphStyle.getTextDirection(), start, end, result);
  }
}
