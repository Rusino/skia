/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ShapedRun.h"

ShapedRun::ShapedRun(const SkFont& font,
          const SkShaper::RunHandler::RunInfo& info,
          int glyphCount,
          SkSpan<const char> text)
    : fFont(font)
    , fInfo(info)
    , fGlyphs   (glyphCount)
    , fPositions(glyphCount)
    , fText(text)
    , fShift(0)
{
  fGlyphs   .push_back_n(glyphCount);
  fPositions.push_back_n(glyphCount);
}

SkVector ShapedRun::finish(SkVector advance)
{
  SkTextBlobBuilder builder;
  const auto wordSize = fGlyphs.size();
  const auto& blobBuffer = builder.allocRunPos(fFont, SkToInt(wordSize));

  sk_careful_memcpy(blobBuffer.glyphs,
                    fGlyphs.data(),
                    wordSize * sizeof(SkGlyphID));

  for (size_t i = 0; i < wordSize; ++i) {
    blobBuffer.points()[i] = fPositions[SkToInt(i)] + advance;
  }

  fBlob = builder.make();
  fRect = SkRect::MakeLTRB(
      advance.fX,
      advance.fY,
      advance.fX + fInfo.fAdvance.fX,
      advance.fY + fInfo.fDescent + fInfo.fLeading - fInfo.fAscent);

  return fInfo.fAdvance;
}

SkShaper::RunHandler::Buffer ShapedRun::newRunBuffer()
{
  return {
      fGlyphs   .data(),
      fPositions.data(),
      nullptr
  };
}

void ShapedRun::PaintShadow(SkCanvas* canvas, SkPoint offset)
{
  if (textStyle.getShadowNumber() == 0) {
    return;
  }

  for (SkTextShadow shadow : textStyle.getShadows()) {
    if (!shadow.hasShadow()) {
      continue;
    }

    SkPaint paint;
    paint.setColor(shadow.color);
    if (shadow.blur_radius != 0.0) {
      paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, shadow.blur_radius, false));
    }
    canvas->drawTextBlob(fBlob, offset.x() + shadow.offset.x(), offset.y() + shadow.offset.y(), paint);
  }
}

void ShapedRun::PaintBackground(SkCanvas* canvas, SkPoint offset)
{
  if (!textStyle.hasBackground()) {
    return;
  }
  fRect.offset(offset.fY, offset.fY);
  canvas->drawRect(fRect, textStyle.getBackground());
}

SkScalar ShapedRun::ComputeDecorationThickness(SkTextStyle textStyle)
{
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

SkScalar ShapedRun::ComputeDecorationPosition(SkScalar thickness)
{
  SkFontMetrics metrics;
  textStyle.getFontMetrics(metrics);

  SkScalar position;

  switch (textStyle.getDecoration()) {
    case SkTextDecoration::kUnderline:
      if (metrics.hasUnderlinePosition(&position)) {
        return position - metrics.fAscent;
      } else {
        position = metrics.fDescent - metrics.fAscent;
        if (textStyle.getDecorationStyle() == SkTextDecorationStyle::kWavy ||
            textStyle.getDecorationStyle() == SkTextDecorationStyle::kDouble
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
      SkScalar delta = fRect.height() - (metrics.fDescent - metrics.fAscent + metrics.fLeading);
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

void ShapedRun::ComputeDecorationPaint(SkPaint& paint, SkPath& path, SkScalar width)
{
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

void ShapedRun::PaintDecorations(SkCanvas* canvas, SkPoint offset, SkScalar width)
{
  if (textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  // Decoration thickness
  SkScalar thickness = ComputeDecorationThickness(textStyle);

  // Decoration position
  SkScalar position = ComputeDecorationPosition(thickness);

  // Decoration paint (for now) and/or path
  SkPaint paint;
  SkPath path;
  ComputeDecorationPaint(paint, path, width);
  paint.setStrokeWidth(thickness);

  // Draw the decoration
  SkScalar x = offset.x() + fRect.left() + fShift;
  SkScalar y = offset.y() + fRect.top() + position;
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
      canvas->drawLine(x, y, x + width, y, paint);
      break;
    default:
      break;
  }
}

void ShapedRun::Paint(SkCanvas* canvas, SkTextStyle style, SkPoint& point)
{
  SkPoint start = SkPoint::Make(point.x() + fShift, point.y());
  PaintBackground(canvas, start);
  PaintShadow(canvas, start);

  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.reset();
    paint.setColor(style.getColor());
  }
  paint.setAntiAlias(true);
  canvas->drawTextBlob(fBlob, start.x(), start.y(), paint);

  PaintDecorations(canvas, start, fRect.width());

  point.fX += fInfo.fAdvance.fX;
}
