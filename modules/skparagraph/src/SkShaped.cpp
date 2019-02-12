/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkShaped.h"
#include "SkFontMetrics.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"

ShapedParagraph::ShapedParagraph(SkTextBlobBuilder* builder, SkParagraphStyle style, std::vector<StyledText> styles)
: _style(style)
, _styles(std::move(styles))
{
  _builder = builder;
  _alphabeticBaseline = 0;
  _ideographicBaseline = 0;
  _height = 0;
  _width = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
  _linesNumber = 0;
  _exceededLimits = false;
  _currentChar = start();
  _maxAscend = 0;
  _maxDescend = 0;
  _maxLeading = 0;
}

void ShapedParagraph::layout(SkScalar maxWidth, size_t maxLines) {
  _maxWidth = maxWidth;
  _maxLines = maxLines;
  _linesNumber = 0;

  if (!_styles.empty()) {
    auto start = _styles.begin()->start;
    auto end = _styles.empty() ? start - 1 : std::prev(_styles.end())->end;

    if (start < end) {
      MultipleFontRunIterator font(start, end - start,
                                   _styles.begin(),
                                   _styles.end(),
                                   _style.getTextStyle());
      SkShaper shaper(nullptr);
      shaper.shape(this, &font, start, end - start, true, {0, 0}, maxWidth);
      return;
    }

    // Shaper does not shape empty lines
    SkFontMetrics metrics;
    _styles.back().textStyle.getFontMetrics(metrics);
    _alphabeticBaseline = - metrics.fAscent;
    _ideographicBaseline = - metrics.fAscent;
    _height = metrics.fDescent + metrics.fLeading - metrics.fAscent;
    _width = 0;
    _maxIntrinsicWidth = 0;
    _minIntrinsicWidth = 0;
    _linesNumber = 1;
    return;
  }

  // Shaper does not shape empty lines
  _height = 0;
  _width = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
}

void ShapedParagraph::printBlocks(size_t linenum) {
  SkDebugf("Paragraph #%d\n", linenum);
  if (!_styles.empty()) {
    SkDebugf("Lost blocks\n");
    for (auto& block : _styles) {
      std::string str(block.start, block.end - block.start);
      SkDebugf("Block: '%s'\n", str.c_str());
    }
  }
  int i = 0;
  for (auto& line : _lines) {
    SkDebugf("Line: %d (%d)\n", i, line.words.size());
    for (auto& word : line.words) {
      std::string str(word.start, word.end - word.start);
      SkDebugf("Block: '%s'\n", str.c_str());
    }
    ++i;
  }
}

void ShapedParagraph::format() {

  size_t lineIndex = 0;
  for (auto& line : _lines) {

    ++lineIndex;
    SkScalar delta = _maxWidth - line.size.width();
    if (delta <= 0) {
      // Delta can be < 0 if there are extra whitespaces at the end of the line;
      // This is a limitation of a current version
      continue;
    }

    switch (_style.effective_align()) {
      case SkTextAlign::left:
        break;
      case SkTextAlign::right:
        for (auto& word : line.words) {
          word.shift += delta;
        }
        line.size.fWidth = _maxWidth;
        _width = _maxWidth;
        break;
      case SkTextAlign::center: {
        auto half = delta / 2;
        for (auto& word : line.words) {
          word.shift += half;
        }
        line.size.fWidth = _maxWidth;
        _width = _maxWidth;
        break;
      }
      case SkTextAlign::justify: {
        if (&line == &_lines.back()) {
          break;
        }
        SkScalar step = delta / (line.words.size() - 1);
        SkScalar shift = 0;
        for (auto& word : line.words) {
          word.shift += shift;
          if (&word != &line.words.back()) {
            word.rect.fRight += step;
            line.size.fWidth = _maxWidth;
            _width = _maxWidth;
          }
          shift += step;
        }
        break;
      }
      default:
        break;
    }
  }
}

// TODO: currently we pick the first style of the run and go with it regardless
void ShapedParagraph::paint(SkCanvas* textCanvas, SkPoint& point) {

  std::vector<StyledText>::iterator firstStyle = _styles.begin();
  for (auto& line : _lines) {
    for (auto word : line.words) {

      // Find the first style that affects the run
      while (firstStyle != _styles.end() && firstStyle->end < word.start) {
        ++firstStyle;
      }
      word.textStyle = firstStyle == _styles.end() ? _style.getTextStyle() : firstStyle->textStyle;

      // Draw all backgrounds and shadows for all the styles that affect the run
      SkPoint start = SkPoint::Make(point.x() + word.shift, point.y());
      PaintBackground(textCanvas, word, start);
      PaintShadow(textCanvas, word, start);

      SkTextStyle style = firstStyle == _styles.end() ? _style.getTextStyle() : firstStyle->textStyle;
      SkPaint paint;
      if (style.hasForeground()) {
        paint = style.getForeground();
      } else {
        paint.reset();
        paint.setColor(style.getColor());
      }
      paint.setAntiAlias(true);
      textCanvas->drawTextBlob(word.blob, start.x(), start.y(), paint);

      PaintDecorations(textCanvas, word, start, word.rect.width());
    }
  }
  point.fY += _height;
}

SkScalar ShapedParagraph::ComputeDecorationThickness(SkTextStyle textStyle) {

  SkScalar thickness = 1.0f;

  SkFontMetrics metrics;
  textStyle.getFontMetrics(metrics);

  switch (textStyle.getDecoration()) {
    case SkTextDecoration::kUnderline:
      if (!metrics.hasUnderlineThickness(&thickness)) {
        thickness = 1.f;
      }
      break;
    case SkTextDecoration::kOverline:
      break;
    case SkTextDecoration::kLineThrough:
      if (!metrics.hasStrikeoutThickness(&thickness)) {
        thickness = 1.f;
      }
      break;
    default:
      SkASSERT(false);
  }

  thickness = SkMaxScalar(thickness, textStyle.getFontSize() / 14.f);

  return thickness * textStyle.getDecorationThicknessMultiplier();
}

SkScalar ShapedParagraph::ComputeDecorationPosition(Word word, SkScalar thickness) {

  SkFontMetrics metrics;
  word.textStyle.getFontMetrics(metrics);

  SkScalar position;

  switch (word.textStyle.getDecoration()) {
    case SkTextDecoration::kUnderline:
      if (metrics.hasUnderlinePosition(&position)) {
        return position - metrics.fAscent;
      } else {
        position = metrics.fDescent - metrics.fAscent;
        if (word.textStyle.getDecorationStyle() == SkTextDecorationStyle::kWavy ||
            word.textStyle.getDecorationStyle() == SkTextDecorationStyle::kDouble
            ) {
          return position - thickness * 3;
        } else {
          return position - thickness;
        }
      }

      break;
    case SkTextDecoration::kOverline:
      return 0;
      break;
    case SkTextDecoration::kLineThrough: {
      SkScalar delta = word.rect.height() - (metrics.fDescent - metrics.fAscent + metrics.fLeading);
      position = SkTMax(0.0f, delta) + (metrics.fDescent - metrics.fAscent) / 2;
      break;
    }
    default:
      position = 0;
      SkASSERT(false);
      break;
  }

  return position;
}

void ShapedParagraph::ComputeDecorationPaint(Word word, SkPaint& paint, SkPath& path, SkScalar width) {

  paint.setStyle(SkPaint::kStroke_Style);
  if (word.textStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(word.textStyle.getColor());
  } else {
    paint.setColor(word.textStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);

  SkScalar scaleFactor = word.textStyle.getFontSize() / 14.f;

  switch (word.textStyle.getDecorationStyle()) {
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
          SkDashPathEffect::Make(intervals, count, 0.0f),
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
          SkDashPathEffect::Make(intervals, count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
    case SkTextDecorationStyle::kWavy: {

      int wave_count = 0;
      double x_start = 0;
      double wavelength = 2 * scaleFactor;

      path.moveTo(0, 0);
      while (x_start + wavelength * 2 < width) {
        path.rQuadTo(wavelength, wave_count % 2 != 0 ? wavelength : -wavelength,
                     wavelength * 2, 0);
        x_start += wavelength * 2;
        ++wave_count;
      }
      break;
    }
  }
}

void ShapedParagraph::PaintDecorations(SkCanvas* canvas,
                      Word word,
                      SkPoint offset,
                      SkScalar width) {

  if (word.textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  // Decoration thickness
  SkScalar thickness = ComputeDecorationThickness(word.textStyle);

  // Decoration position
  SkScalar position = ComputeDecorationPosition(word, thickness);

  // Decoration paint (for now) and/or path
  SkPaint paint;
  SkPath path;
  ComputeDecorationPaint(word, paint, path, width);
  paint.setStrokeWidth(thickness);

  // Draw the decoration
  SkScalar x = offset.x() + word.rect.left() + word.shift;
  SkScalar y = offset.y() + word.rect.top() + position;
  switch (word.textStyle.getDecorationStyle()) {
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
      canvas->drawLine(x, y, x + width, y, paint);
      break;
    default:
      break;
  }
}

void ShapedParagraph::GetRectsForRange(const char* start, const char* end, std::vector<SkTextBox>& result) {
  for (auto& line : _lines) {
    for (auto& word : line.words) {
      if (word.end <= start || word.start >= end) {
        continue;
      }
      result.emplace_back(word.rect, SkTextDirection::ltr); // TODO: the right direction
    }
  }
}