/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <algorithm>

#include "unicode/utypes.h"
#include "unicode/unistr.h"

#include "SkParagraph.h"
#include "SkCanvas.h"
#include "SkSize.h"
#include "SkPath.h"

#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "SkMaskFilter.h"
#include "SkPictureRecorder.h"

static const float kDoubleDecorationSpacing = 3.0f;

SkParagraph::SkParagraph()
    : _picture(nullptr) {}

SkParagraph::~SkParagraph() = default;

double SkParagraph::GetMaxWidth() {
  return SkScalarToDouble(_width);
}

double SkParagraph::GetHeight() {
  return SkScalarToDouble(_height);
}

double SkParagraph::GetMinIntrinsicWidth() {
  return SkScalarToDouble(_minIntrinsicWidth);
}

double SkParagraph::GetMaxIntrinsicWidth() {
  return SkScalarToDouble(_maxIntrinsicWidth);
}

double SkParagraph::GetAlphabeticBaseline() {
  // TODO: implement
  return SkScalarToDouble(_alphabeticBaseline);
}

double SkParagraph::GetIdeographicBaseline() {
  // TODO: implement
  return SkScalarToDouble(_ideographicBaseline);
}

bool SkParagraph::DidExceedMaxLines() {
  return _linesNumber > _style.getMaxLines();
}

void SkParagraph::SetText(std::vector<uint16_t> utf16text) {

  _text16 = std::move(utf16text);
/*
  icu::UnicodeString utf16 = icu::UnicodeString(&_text16[0], _text16.size());
  std::string str;
  utf16.toUTF8String(str);
  SkDebugf("SetText '%s'\n", str.c_str());
*/
}

void SkParagraph::SetText(const char* utf8text, size_t textBytes) {

  icu::UnicodeString utf16 = icu::UnicodeString::fromUTF8(icu::StringPiece(utf8text, textBytes));
  std::string str;
  utf16.toUTF8String(str);

  _text16.resize(textBytes + 1);
  memcpy(&_text16[0], utf16.getBuffer(), textBytes * sizeof(uint16_t));
}

void SkParagraph::SetStyles(std::vector<StyledText> styles) {
  _styles = std::move(styles);
}

void SkParagraph::SetParagraphStyle(SkParagraphStyle style) {
  _style = style;
}

bool SkParagraph::Layout(double width) {

  if (_text16.empty()) {
    // TODO: ???
    return true;
  }

  // Collect Flutter values
  _alphabeticBaseline = 0;
  _height = 0;
  _width = 0;
  _ideographicBaseline = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
  _linesNumber = 0;

  // Initialize styledRuns - the result of shaping
  _styledRuns.reserve(_styles.size());
  for (auto run : _styles) {
    _styledRuns.emplace_back(run.start, run.end, nullptr, SkRect(), run.textStyle);
  }

  // Shape the text
  SkShaper shaper(&_text16[0], _text16.size(), _styles.begin(), _styles.end(), _style.getTextStyle(), _fontCollection );

  if (!shaper.generateGlyphs()) {
    SkDebugf("Error shaping\n");
    return false;
  }

  // Iterate over the glyphs in logical order to mark line endings.
  bool breakable = shaper.generateLineBreaks(SkDoubleToScalar(width));

  // Reorder the runs and glyphs per line and write them out.
  auto style = _styledRuns.begin();

  // TODO: simplify the logic
  SkTextBlobBuilder bigBuilder;
  shaper.refineLineBreaks(&bigBuilder, {0, 0},
      [this, &style, &shaper]
      (const ShapedRun& run, size_t s, size_t e, SkPoint point, SkRect background) {
        size_t zero = run.fUtf16Start - &_text16[0]; // Number of characters before this shaped run
        size_t lineStart =  zero + s;
        size_t lineEnd = zero + e;
        if (style->end < lineStart || style->start > lineEnd) {
          return;
        }

        SkPoint currentPoint = point;
        while (true) {

          size_t startGlyphIndex = std::max<size_t>(style->start, lineStart) - zero;
          size_t endGlyphIndex = std::min<size_t>(style->end, lineEnd) - zero;
          icu::UnicodeString utf16 = icu::UnicodeString(run.fUtf16Start, run.fUtf16End - run.fUtf16Start);
          std::string str;
          utf16.toUTF8String(str);
          SkDebugf("Block  %d:%d '%s'\n", startGlyphIndex, endGlyphIndex, str.c_str());
          SkTextBlobBuilder builder;
          shaper.append(&builder, run, startGlyphIndex, endGlyphIndex, &currentPoint);
          style->blob = builder.make();
          style->rect = background;

          if (style->end < lineEnd) {
            // Style ended but the line didn't; continue
            ++style;
          } else if (style->end == lineEnd) {
            // End of line is the end of style; move on
            ++style;
            break;
          } else {
            // Style is bigger than the line; we need to break it
            style = _styledRuns.emplace(style, style->start, lineEnd, style->blob, style->rect, style->textStyle);
            ++style;
            style->start = lineEnd;
            break;
          }
        }
      },
      [this](size_t line_number,
             SkSize size,
             int previousRunIndex,
             int runIndex) {
        _linesNumber = line_number;
        _height = SkMaxScalar(_height, size.fHeight);
        _width = SkMaxScalar(_width, size.fWidth);
        _maxIntrinsicWidth += size.fWidth;
      });

  if (breakable || false) {
    shaper.breakIntoWords([this](SkSize size, int startIndex, int nextStartIndex) {
      _minIntrinsicWidth = SkMaxScalar(_minIntrinsicWidth, size.fWidth);
    });
  }

  if (_picture == nullptr) {
    RecordPicture();
  }

  return true;
}

void SkParagraph::RecordPicture() {

  SkPictureRecorder recorder;
  SkCanvas* textCanvas = recorder.beginRecording(_width, _height, nullptr, 0);

  SkPoint point = SkPoint::Make(0, 0);
  for (auto& run : _styledRuns) {

    SkPaint paint;
    if (run.textStyle.hasForeground()) {
      paint = run.textStyle.getForeground();
    } else {
      paint.reset();
      paint.setColor(run.textStyle.getColor());
    }

    paint.setAntiAlias(true);
    paint.setLCDRenderText(true);
    paint.setTextSize(run.textStyle.getFontSize());
    paint.setTypeface(run.textStyle.getTypeface());

    PaintBackground(textCanvas, run, point);
    PaintShadow(textCanvas, run, point);
    textCanvas->drawTextBlob(run.blob, point.x(), point.y(), paint);
    PaintDecorations(textCanvas, run, point);
  }

  _picture = recorder.finishRecordingAsPicture();
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) const {

  if (_text16.empty()) {
    // TODO: ???
    return;
  }

  SkMatrix matrix = SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
  canvas->drawPicture(_picture, &matrix, nullptr);
}

void SkParagraph::PaintDecorations(SkCanvas* canvas,
                                   StyledRun run,
                                   SkPoint offset) {
  if (run.textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  SkPaint paint;
  // Set stroke
  paint.setStyle(SkPaint::kStroke_Style);
  // Set color
  if (run.textStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(run.textStyle.getColor());
  } else {
    paint.setColor(run.textStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setTextSize(run.textStyle.getFontSize());
  paint.setTypeface(SkTypeface::MakeFromName(run.textStyle.getFontFamily().data(), run.textStyle.getFontStyle()));

  // This is set to 2 for the double line style
  int decoration_count = 1;

  // Filled when drawing wavy decorations.
  SkPath path;
  SkScalar width = run.rect.width();
  SkFontMetrics metrics;
  run.textStyle.getFontMetrics(metrics);
  SkScalar underline_thickness;
  if ((metrics.fFlags &
      SkFontMetrics::FontMetricsFlags::kUnderlineThicknessIsValid_Flag) &&
      metrics.fUnderlineThickness > 0) {
    underline_thickness = metrics.fUnderlineThickness;
  } else {
    // Backup value if the fUnderlineThickness metric is not available:
    // Divide by 14pt as it is the default size.
    underline_thickness = run.textStyle.getFontSize() / 14.0f;
  }
  paint.setStrokeWidth(underline_thickness * run.textStyle.getDecorationThicknessMultiplier());

  auto bounds = run.rect;
  SkScalar x = offset.x() + bounds.x();
  SkScalar y = offset.y() + bounds.y();

  // Setup the decorations
  switch (run.textStyle.getDecorationStyle()) {
    case SkTextDecorationStyle::kSolid: {
      break;
    }
    case SkTextDecorationStyle::kDouble: {
      decoration_count = 2;
      break;
    }
      // Note: the intervals are scaled by the thickness of the line, so it is
      // possible to change spacing by changing the decoration_thickness
      // property of TextStyle.
    case SkTextDecorationStyle::kDotted: {
      // Divide by 14pt as it is the default size.
      const float scale = run.textStyle.getFontSize() / 14.0f;
      const SkScalar intervals[] = {1.0f * scale, 1.5f * scale, 1.0f * scale, 1.5f * scale};
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
      // Divide by 14pt as it is the default size.
      const float scale = run.textStyle.getFontSize() / 14.0f;
      const SkScalar intervals[] = {4.0f * scale, 2.0f * scale, 4.0f * scale,
                                    2.0f * scale};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
    case SkTextDecorationStyle::kWavy: {
      int wave_count = 0;
      double x_start = 0;
      double wavelength = underline_thickness * run.textStyle.getDecorationThicknessMultiplier() * 2;

      path.moveTo(x, y);
      while (x_start + wavelength * 2 < width) {
        path.rQuadTo(wavelength, wave_count % 2 != 0 ? wavelength : -wavelength,
                     wavelength * 2, 0);
        x_start += wavelength * 2;
        ++wave_count;
      }
      break;
    }
  }

  // Draw the decorations.
  // Use a for loop for "kDouble" decoration style
  for (int i = 0; i < decoration_count; i++) {
    double y_offset = i * underline_thickness * kDoubleDecorationSpacing;
    double y_offset_original = y_offset;
    // Underline
    if (run.textStyle.getDecoration() & SkTextDecoration::kUnderline) {
      y_offset += run.rect.height() - metrics.fDescent;
      y_offset +=
          (metrics.fFlags &
              SkFontMetrics::FontMetricsFlags::kUnderlinePositionIsValid_Flag)
          ? metrics.fUnderlinePosition
          : underline_thickness;
      if (run.textStyle.getDecorationStyle() != SkTextDecorationStyle::kWavy) {
        canvas->drawLine(x, y + y_offset, x + width, y + y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
    // Overline
    if (run.textStyle.getDecoration() & SkTextDecoration::kOverline) {
      // We subtract fAscent here because for double overlines, we want the
      // second line to be above, not below the first.
      //y_offset -= metrics.fAscent;
      if (run.textStyle.getDecorationStyle() != SkTextDecorationStyle::kWavy) {
        canvas->drawLine(x, y - y_offset, x + width, y - y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, -y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
    // Strikethrough
    if (run.textStyle.getDecoration() & SkTextDecoration::kLineThrough) {
      if (metrics.fFlags &
          SkFontMetrics::FontMetricsFlags::kStrikeoutThicknessIsValid_Flag) {
        paint.setStrokeWidth(metrics.fStrikeoutThickness * run.textStyle.getDecorationThicknessMultiplier());
        y_offset = i * metrics.fStrikeoutThickness * kDoubleDecorationSpacing * 5;
      }
      // Make sure the double line is "centered" vertically.
      //y_offset += (decoration_count - 1.0) * underline_thickness *
      //    kDoubleDecorationSpacing / -2.0;

      y_offset += run.rect.height()/2;
      y_offset -=
          (metrics.fFlags & SkFontMetrics::FontMetricsFlags::kStrikeoutThicknessIsValid_Flag)
          ? metrics.fStrikeoutPosition
          : metrics.fXHeight / -2.0;
      if (run.textStyle.getDecorationStyle() != SkTextDecorationStyle::kWavy) {
        canvas->drawLine(x, y + y_offset, x + width, y + y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
  }
}

void SkParagraph::PaintBackground(SkCanvas* canvas,
                                  StyledRun run,
                                  SkPoint offset) {

  if (!run.textStyle.hasBackground()) {
    return;
  }
  canvas->drawRect(run.rect, run.textStyle.getBackground());
}

void SkParagraph::PaintShadow(SkCanvas* canvas,
                              StyledRun run,
                              SkPoint offset) {
  if (run.textStyle.getShadowNumber() == 0) {
    return;
  }

  for (SkTextShadow shadow : run.textStyle.getShadows()) {
    if (!shadow.hasShadow()) {
      continue;
    }

    SkPaint paint;
    paint.setColor(shadow.color);
    if (shadow.blur_radius != 0.0) {
      paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, shadow.blur_radius, false));
    }
    canvas->drawTextBlob(run.blob, offset.x() + shadow.offset.x(), offset.y() + shadow.offset.y(), paint);
  }
}

std::vector<SkTextBox> SkParagraph::GetRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rect_height_style,
    RectWidthStyle rect_width_style) {
  // TODO: implement
  //SkASSERT(false);
  std::vector<SkTextBox> result;
  return result;
}

SkPositionWithAffinity SkParagraph::GetGlyphPositionAtCoordinate(double dx, double dy) const {
  // TODO: implement
  //SkASSERT(false);
  return SkPositionWithAffinity(0, Affinity::UPSTREAM);
}

SkRange<size_t> SkParagraph::GetWordBoundary(unsigned offset) {
  // TODO: implement
  SkASSERT(false);
  SkRange<size_t> result;
  return result;
}