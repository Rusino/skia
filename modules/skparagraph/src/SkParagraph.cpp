/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <algorithm>

#include <unicode/brkiter.h>
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

void SkParagraph::Runs(std::vector<StyledText> styles) {
  _styles = std::move(styles);
}

void SkParagraph::SetParagraphStyle(SkParagraphStyle style) {
  _style = style;
}

bool SkParagraph::Layout(double width) {

  // Collect Flutter values
  _alphabeticBaseline = 0;
  _height = 0;
  _width = 0;
  _ideographicBaseline = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
  _linesNumber = 0;

  // Break the text into lines (with each one broken into blocks by style)
  BreakLines();

  auto iter = _lines.begin();
  while (iter != _lines.end()) {
    if (!LayoutLine(iter, width)) {
      return false;
    }
    ++iter;
  }

  RecordPicture();

  return true;
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) const {

  SkMatrix matrix = SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
  canvas->drawPicture(_picture, &matrix, nullptr);
}

bool SkParagraph::LayoutLine(std::vector<Line>::iterator& line, SkScalar width) {

  if (line->IsEmpty()) {
    return true;
  }

  SkShaper shaper(&_text16[line->Start()], line->Length(),
                  line->blocks.begin(), line->blocks.end(),
                  _style.getTextStyle(), _fontCollection);

  if (!shaper.generateGlyphs()) {
    SkDebugf("Error shaping\n");
    return false;
  }

  // Iterate over the glyphs in logical order to mark line endings.
  bool breakable = shaper.generateLineBreaks(SkDoubleToScalar(width));

  // Reorder the runs and glyphs per line and write them out.
  auto block = line->blocks.begin();

  // TODO: simplify the logic
  SkTextBlobBuilder bigBuilder;
  shaper.refineLineBreaks(&bigBuilder, {0, 0},
                          [this, &block, &shaper, &line]
                              (const ShapedRun& run, size_t s, size_t e, SkPoint point, SkRect background) {
                            size_t zero = run.fUtf16Start - &_text16[0]; // Number of characters before this shaped run
                            size_t lineStart =  zero + s;
                            size_t lineEnd = zero + e;
                            //if (block->end < lineStart || block->start > lineEnd) {
                            //  return;
                            //}

                            icu::UnicodeString utf16 = icu::UnicodeString(run.fUtf16Start + s, e - s);
                            std::string str;
                            utf16.toUTF8String(str);
                            SkDebugf("Shaped run: %d:%d'%s'\n", s, e, str.c_str());

                            SkPoint currentPoint = point;
                            // Only the first block has the background
                            bool firstBlockInTheRun = true;
                            while (true) {

                              size_t startGlyphIndex = std::max<size_t>(block->start, lineStart) - zero;
                              size_t endGlyphIndex = std::min<size_t>(block->end, lineEnd) - zero;
                              icu::UnicodeString utf16 = icu::UnicodeString(run.fUtf16Start + startGlyphIndex, endGlyphIndex - startGlyphIndex);
                              std::string str;
                              utf16.toUTF8String(str);
                              SkDebugf("Block  %d:%d '%s'\n", startGlyphIndex, endGlyphIndex, str.c_str());
                              SkTextBlobBuilder builder;
                              shaper.append(&builder, run, startGlyphIndex, endGlyphIndex, &currentPoint);
                              block->blob = builder.make();
                              block->rect = firstBlockInTheRun ? background : SkRect::MakeXYWH(0,0,0,0);
                              firstBlockInTheRun = false;

                              if (block->end < lineEnd) {
                                // Style ended but the line didn't; continue
                                ++block;
                              } else if (block->end == lineEnd) {
                                // End of line is the end of style; move on
                                ++block;
                                break;
                              } else {
                                // Style is bigger than the line; we need to break it
                                auto oldEnd = block->end;
                                block->end = lineEnd;
                                block = line->blocks.emplace(std::next(block), lineEnd, oldEnd, block->blob, block->rect, block->textStyle);
                                break;
                              }
                            }
                          },
                          [&line, &block, this](
                              bool lineBreak,
                              size_t line_number,
                              SkSize size,
                              SkScalar spacer,
                              int previousRunIndex,
                              int runIndex) {
                            line->size = size;
                            line->spacer = spacer;
                            _height += line->size.height();
                            _width = SkMaxScalar(_width, line->size.width());
                            if (lineBreak) {
                              std::vector<Block> tail;
                              if (block != line->blocks.end()) {
                                std::move(block,
                                          line->blocks.end(),
                                          std::back_inserter(tail));
                                line->blocks.erase(block, line->blocks.end());
                              }
                              line = _lines.emplace(std::next(line), tail, false);
                              block = line->blocks.begin();
                            }
                          });

  if (breakable || false) {
    shaper.breakIntoWords([this](SkSize size, int startIndex, int nextStartIndex) {
      _minIntrinsicWidth = SkMaxScalar(_minIntrinsicWidth, size.fWidth);
    });
  }

  return true;
}

void SkParagraph::RecordPicture() {

  SkPictureRecorder recorder;
  SkCanvas* textCanvas = recorder.beginRecording(_width, _height, nullptr, 0);

  SkPoint point = SkPoint::Make(0, 0);
  SkScalar shift = 0;
  for (auto& line : _lines) {

    if (line.hardBreak) {
      textCanvas->translate(0, shift);
    }
    PaintLine(textCanvas, point, line);
    shift = line.size.height();
    //point.fY += line.spacer;
  }

  _picture = recorder.finishRecordingAsPicture();
}

void SkParagraph::PaintLine(SkCanvas* textCanvas, SkPoint point, const Line& line) const {

  for (auto& block : line.blocks) {
    SkPaint paint;
    if (block.textStyle.hasForeground()) {
      paint = block.textStyle.getForeground();
    } else {
      paint.reset();
      paint.setColor(block.textStyle.getColor());
    }

    paint.setAntiAlias(true);
    paint.setLCDRenderText(true);
    paint.setTextSize(block.textStyle.getFontSize());
    paint.setTypeface(block.textStyle.getTypeface());

    PaintBackground(textCanvas, block, point);
    PaintShadow(textCanvas, block, point);
    textCanvas->drawTextBlob(block.blob, point.x(), point.y(), paint);
    PaintDecorations(textCanvas, block, point);
  }
}

void SkParagraph::PaintDecorations(SkCanvas* canvas,
                                   Block block,
                                   SkPoint offset) const {
  if (block.textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  SkPaint paint;
  // Set stroke
  paint.setStyle(SkPaint::kStroke_Style);
  // Set color
  if (block.textStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(block.textStyle.getColor());
  } else {
    paint.setColor(block.textStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setTextSize(block.textStyle.getFontSize());
  paint.setTypeface(SkTypeface::MakeFromName(block.textStyle.getFontFamily().data(), block.textStyle.getFontStyle()));

  // This is set to 2 for the double line style
  int decoration_count = 1;

  // Filled when drawing wavy decorations.
  SkPath path;
  SkScalar width = block.rect.width();
  SkFontMetrics metrics;
  block.textStyle.getFontMetrics(metrics);
  SkScalar underline_thickness;
  if ((metrics.fFlags &
      SkFontMetrics::FontMetricsFlags::kUnderlineThicknessIsValid_Flag) &&
      metrics.fUnderlineThickness > 0) {
    underline_thickness = metrics.fUnderlineThickness;
  } else {
    // Backup value if the fUnderlineThickness metric is not available:
    // Divide by 14pt as it is the default size.
    underline_thickness = block.textStyle.getFontSize() / 14.0f;
  }
  paint.setStrokeWidth(underline_thickness * block.textStyle.getDecorationThicknessMultiplier());

  auto bounds = block.rect;
  SkScalar x = offset.x() + bounds.x();
  SkScalar y = offset.y() + bounds.y();

  // Setup the decorations
  switch (block.textStyle.getDecorationStyle()) {
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
      const float scale = block.textStyle.getFontSize() / 14.0f;
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
      const float scale = block.textStyle.getFontSize() / 14.0f;
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
      double wavelength = underline_thickness * block.textStyle.getDecorationThicknessMultiplier() * 2;

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
    if (block.textStyle.getDecoration() & SkTextDecoration::kUnderline) {
      y_offset += block.rect.height() - metrics.fDescent;
      y_offset +=
          (metrics.fFlags &
              SkFontMetrics::FontMetricsFlags::kUnderlinePositionIsValid_Flag)
          ? metrics.fUnderlinePosition
          : underline_thickness;
      if (block.textStyle.getDecorationStyle() != SkTextDecorationStyle::kWavy) {
        canvas->drawLine(x, y + y_offset, x + width, y + y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
    // Overline
    if (block.textStyle.getDecoration() & SkTextDecoration::kOverline) {
      // We subtract fAscent here because for double overlines, we want the
      // second line to be above, not below the first.
      //y_offset -= metrics.fAscent;
      if (block.textStyle.getDecorationStyle() != SkTextDecorationStyle::kWavy) {
        canvas->drawLine(x, y - y_offset, x + width, y - y_offset, paint);
      } else {
        SkPath offsetPath = path;
        offsetPath.offset(0, -y_offset);
        canvas->drawPath(offsetPath, paint);
      }
      y_offset = y_offset_original;
    }
    // Strikethrough
    if (block.textStyle.getDecoration() & SkTextDecoration::kLineThrough) {
      if (metrics.fFlags &
          SkFontMetrics::FontMetricsFlags::kStrikeoutThicknessIsValid_Flag) {
        paint.setStrokeWidth(metrics.fStrikeoutThickness * block.textStyle.getDecorationThicknessMultiplier());
        y_offset = i * metrics.fStrikeoutThickness * kDoubleDecorationSpacing * 5;
      }
      // Make sure the double line is "centered" vertically.
      //y_offset += (decoration_count - 1.0) * underline_thickness *
      //    kDoubleDecorationSpacing / -2.0;

      y_offset += block.rect.height()/2;
      y_offset -=
          (metrics.fFlags & SkFontMetrics::FontMetricsFlags::kStrikeoutThicknessIsValid_Flag)
          ? metrics.fStrikeoutPosition
          : metrics.fXHeight / -2.0;
      if (block.textStyle.getDecorationStyle() != SkTextDecorationStyle::kWavy) {
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

void SkParagraph::PaintBackground(SkCanvas* canvas, Block block, SkPoint offset) const {

  if (!block.textStyle.hasBackground()) {
    return;
  }
  canvas->drawRect(block.rect, block.textStyle.getBackground());
}

void SkParagraph::PaintShadow(SkCanvas* canvas, Block block, SkPoint offset) const {
  if (block.textStyle.getShadowNumber() == 0) {
    return;
  }

  for (SkTextShadow shadow : block.textStyle.getShadows()) {
    if (!shadow.hasShadow()) {
      continue;
    }

    SkPaint paint;
    paint.setColor(shadow.color);
    if (shadow.blur_radius != 0.0) {
      paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, shadow.blur_radius, false));
    }
    canvas->drawTextBlob(block.blob, offset.x() + shadow.offset.x(), offset.y() + shadow.offset.y(), paint);
  }
}

void SkParagraph::BreakLines() {

  icu::Locale thai("th");
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::BreakIterator> breaker;
  breaker.reset(icu::BreakIterator::createLineInstance(thai, status));
  if (U_FAILURE(status)) {
    SkDebugf("Could not create break iterator: %s", u_errorName(status));
    SK_ABORT("");
  }

  UText utf16UText = UTEXT_INITIALIZER;
  utext_openUChars(&utf16UText, _text16.data(), _text16.size(), &status);
  std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> autoClose(&utf16UText);
  if (U_FAILURE(status)) {
    SkDebugf("Could not create utf16UText: %s", u_errorName(status));
    return;
  }
  breaker->setText(&utf16UText, status);
  if (U_FAILURE(status)) {
    SkDebugf("Could not setText on break iterator: %s", u_errorName(status));
    return;
  }

  size_t firstChar = _text16.size();
  size_t lastChar = _text16.size();

  size_t firstStyle = _styles.size() - 1;
  while (lastChar > 0) {

    firstChar = breaker->preceding(firstChar);
    if ((int32_t)firstChar == icu::BreakIterator::DONE) {
      // Take care of the first line
      firstChar = 0;
    } else if (breaker->getRuleStatus() != UBRK_LINE_HARD) {
      continue;
    }

    int32_t character = *(_text16.begin() + lastChar - 1);
    // Remove all insignificant characters at the end of the line (whitespaces)
    while (lastChar > firstChar && u_isWhitespace(character)) {
      lastChar -= 1;
      character = *(_text16.begin() + lastChar - 1);
    }

    // Find the first style that is related to the line
    while (firstStyle > 0 && _styles[firstStyle].start > firstChar) {
      --firstStyle;
    }

    size_t lastStyle = firstStyle;
    while (lastStyle != _styles.size() && _styles[lastStyle].start < lastChar) {
      ++lastStyle;
    }

    // Generate blocks for future use
    std::vector<Block> blocks;
    if (firstChar != lastChar) {
      blocks.reserve(lastStyle - firstStyle);
      for (auto s = firstStyle; s < lastStyle; ++s) {
        auto& style = _styles[s];
        blocks.emplace_back(
            SkTMax(style.start, firstChar),
            SkTMin(style.end, lastChar),
            style.textStyle);
      }
    }

    // Add one more string to the list
    _lines.emplace(_lines.begin(), blocks, true);

    if (blocks.empty()) {
      // For empty lines we will lose all the styles info after this point, so let's do it here
      auto& textStyle = _styles[firstStyle].textStyle;
      SkFont font(textStyle.getTypeface(), textStyle.getFontSize());
      SkFontMetrics metrics;
      font.getMetrics(&metrics);
      _lines.begin()->size = SkSize::Make(0, metrics.fDescent + metrics.fLeading - metrics.fAscent);
    }

    // Move on
    lastChar = firstChar;
  }

  // Print all lines
  size_t linenum = 0;
  for (auto& line : _lines) {
    auto start = line.Start();
    auto end = line.End();
    icu::UnicodeString utf16 = icu::UnicodeString(&_text16[start], end - start);
    std::string str;
    utf16.toUTF8String(str);
    SkDebugf("Line[%d]: %d:%d '%s'\n", linenum, start, end, str.c_str());
    ++linenum;

    if (line.blocks.empty()) {
      SkDebugf("Empty line\n");
    } else {
      for (auto& block : line.blocks) {
        auto start = block.start;
        auto end = block.end;
        icu::UnicodeString utf16 = icu::UnicodeString(&_text16[start], end - start);
        std::string str;
        utf16.toUTF8String(str);
        SkDebugf("Block: %d:%d '%s'\n", start, end, str.c_str());
      }
    }
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