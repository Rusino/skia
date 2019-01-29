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

void printText(const std::string& label, const UChar* text, size_t start, size_t end) {
  icu::UnicodeString utf16 = icu::UnicodeString(text + start, end - start);
  std::string str;
  utf16.toUTF8String(str);
  SkDebugf("%s: %d:%d'%s'\n", label.c_str(), start, end, str.c_str());
}

SkParagraph::SkParagraph()
    : _exceededLimits(false)
    , _picture(nullptr) {}

SkParagraph::~SkParagraph() = default;

double SkParagraph::GetMaxWidth() {
  return SkScalarToDouble(_width);
}

double SkParagraph::GetHeight() {
  return SkScalarToDouble(_height);
}

double SkParagraph::GetMinIntrinsicWidth() {
  return SkScalarToDouble(_width/*_minIntrinsicWidth*/);
}

double SkParagraph::GetMaxIntrinsicWidth() {
  return SkScalarToDouble(_width /*_maxIntrinsicWidth*/);
}

double SkParagraph::GetAlphabeticBaseline() {
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
}

void SkParagraph::SetText(const char* utf8text, size_t textBytes) {

  icu::UnicodeString utf16 = icu::UnicodeString::fromUTF8(icu::StringPiece(utf8text, textBytes));
  std::string str;
  utf16.toUTF8String(str);

  _text16.resize(textBytes);
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
  /*
  icu::UnicodeString uni = icu::UnicodeString(_text16.data(), _text16.size());
  std::string str;
  uni.toUTF8String(str);
  SkDebugf("Shape: '%s' (%d)\n", str.c_str(), _text16.size());
  */

  // Break the text into lines (with each one broken into blocks by style)
  BreakLines();

  std::vector<Line>::iterator iter = _lines.begin();
  while (iter != _lines.end()) {
    if (!LayoutLine(iter, width)) {
      return false;
    }
    ++_linesNumber;
    iter = std::next(iter);
  }

  for (size_t i = 0; i < _lines.size(); ++i) {
    FormatLine(_lines[i], i == _lines.size() - 1, width);
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

    SkFontMetrics metrics;
    line->blocks[0].textStyle.getFontMetrics(metrics);
    auto height = metrics.fDescent + -metrics.fAscent + metrics.fLeading;
    auto baseline = - metrics.fAscent;

    line->size = SkSize::Make(0, height);
    _height += height;
    _ideographicBaseline = baseline;
    _alphabeticBaseline = baseline;

    return true;
  }

  SkShaper shaper(&_text16[line->Start()], line->Length(),
                  line->blocks.begin(), line->blocks.end(),
                  _style.getTextStyle());

  if (!shaper.generateGlyphs()) {
    return false;
  }

  // Iterate over the glyphs in logical order to mark line endings.
  shaper.generateLineBreaks(SkDoubleToScalar(width));

  // Start iterating from the first block in the line
  auto block = line->blocks.begin();

  shaper.refineLineBreaks(nullptr, {0, 0},
      // Match blocks (styles) and runs (words)
      [this, &block, &line]
      (sk_sp<SkTextBlob> blob, const ShapedRun& run, size_t s, size_t e, SkRect rect) {

        if (_exceededLimits) {
          return;
        }

        _minIntrinsicWidth = SkMaxScalar(_minIntrinsicWidth, rect.width());
        size_t endWord = run.fUtf16Start - &_text16[0] + e;
        size_t startWord = run.fUtf16Start - &_text16[0] + s;

        while (block != line->blocks.end() && startWord >= block->end) {
          ++block;
        }

        SkASSERT(block != line->blocks.end());
        block->blob = blob;
        block->rect = rect;

        //printText("Word", run.fUtf16Start, s, e);
        if (block->end > endWord) {
          // One block (style) can have few runs (words); let's break them here
          // TODO: deal with the trimmed values
          auto oldEnd = block->end;
          block->end = endWord;
          block = line->blocks.emplace(std::next(block),
                                       endWord,
                                       oldEnd,
                                       block->blob,
                                       block->rect,
                                       block->textStyle);
        } else if (block->end == endWord) {
          // Nothing else
        } else {
          // One word is covered by many styles
          // We have 3 solutions here:
          // 1. Stop it by separating runs by any style, not only font-related
          // 2. Take the first style for the entire word (implemented)
          // 3. TODO: Make each run a glyph, not a "word" and deal with it appropriately
          // Remove all the other styles that cover only this word
          while (block != line->blocks.end() && block->end <= endWord) {
            block = line->blocks.erase(block);
          }
        }
      },
      // Create extra lines if required by Shaper
      [&line, &block, width, this](size_t lineNumber, bool endOfText, SkScalar lineWidth, SkScalar height, SkScalar baseline) {

        if (_exceededLimits) {
            // We already exceeded the limits - just ignore everything
            return;
        }

        line->size = SkSize::Make(lineWidth, height);
        _height += height;
        _width = SkMaxScalar(_width, lineWidth);
        _ideographicBaseline = baseline;
        _alphabeticBaseline = baseline;
        _maxIntrinsicWidth += lineWidth;

        // Check if we can and should use ellipsis
        if (!isinf(width) &&
            !endOfText &&                      // We still have some text to shape
           _style.ellipsized() &&                   // We have ellipsis
          (_style.unlimited_lines() ||
           _style.getMaxLines() == lineNumber + 1)) {// The last line
          // This is the last allowed line
          // The line width exceeds the constraint
          while (!line->blocks.empty()) {
            auto& lastBlock = line->blocks.back();
            // Create ellipsis blob to measure it and possibly to add
            SkPoint point = SkPoint::Make(lastBlock.rect.right(), lastBlock.rect.top());
            SkTextBlobBuilder ellipsisBuilder;
            // We cannot create an ellipsis here because it must match the font of the last run
            // We cannot find out the last run until we measure the ellipsis :)
            const std::u16string& ellipsis = _style.getEllipsis();
            SkRect ellipsisRect = SkShaper::shape(&ellipsisBuilder,
                                                  reinterpret_cast<const uint16_t*>(ellipsis.data()),
                                                  ellipsis.size(),
                                                  point,
                                                  lastBlock.textStyle);
            if (ellipsisRect.right() <= width || line->blocks.size() == 1) {
              line->blocks.emplace_back(0, 0,
                                        ellipsisBuilder.make(),
                                        ellipsisRect,
                                        lastBlock.textStyle);
              break;
            }
            line->blocks.pop_back();
          }
          // There might be a case when not even one block fits; nothing is drawn then
          _exceededLimits = true;
          // Remove all the othe lines and ignore the output from Shaper for the rest
          _lines.erase(std::next(line), _lines.end());
        } else {
          // Normal process
          if (!endOfText) {
            // Break blocks between two lines
            std::vector<Block> tail;
            if (block != line->blocks.end()) {
              std::move(block, line->blocks.end(), std::back_inserter(tail));
              line->blocks.erase(block, line->blocks.end());
            }
            // Add one more line and start from the it's first block again
            line = _lines.emplace(std::next(line), tail, false);
            block = line->blocks.begin();
          }
        }

      });
  return true;
}

void SkParagraph::FormatLine(Line& line, bool lastLine, SkScalar width) {

  SkScalar delta = width - line.size.width();
  if (delta == 0) {
    // Nothing to do
    return;
  }

  SkAssertResult(delta >= 0);
  switch (_style.effective_align()) {
    case SkTextAlign::left:
      break;
    case SkTextAlign::right:
      //SkDebugf("Right %f - %f\n", width, delta);
      for (auto& block : line.blocks) {
        block.shift += delta;
      }
      _width = width;
      break;
    case SkTextAlign::center: {
      auto half = delta / 2;
      //SkDebugf("Center %f - %f\n", width, half);
      for (auto& block : line.blocks) {
        block.shift += half;
      }
      _width = width;
      break;
    }
    case SkTextAlign::justify: {
      if (lastLine) {
        //SkDebugf("Justify last line\n");
        break;
      }
      SkScalar step = delta / (line.blocks.size() - 1);
      //SkDebugf("Justify %f - %f\n", width, step);
      SkScalar shift = 0;
      for (auto& block : line.blocks) {
        block.shift += shift;
        if (&block != &line.blocks.back()) {
          block.rect.fRight += step;
          _width = width;
        }
        shift += step;
      }
      break;
    }
    default:
      break;
  }
}

void SkParagraph::RecordPicture() {

  SkPictureRecorder recorder;
  SkCanvas* textCanvas = recorder.beginRecording(_width, _height, nullptr, 0);

  SkPoint point = SkPoint::Make(0, 0);
  SkScalar height = 0;
  size_t linenum = 1;
  for (auto& line : _lines) {

    if (line.hardBreak) {
      point.fY = height;
    }

    PaintLine(textCanvas, point, line);

    height += line.size.height();
    ++linenum;
  }

  _picture = recorder.finishRecordingAsPicture();
}

void SkParagraph::PaintLine(SkCanvas* textCanvas, SkPoint point, const Line& line) {

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

    SkPoint start = SkPoint::Make(point.x() + block.shift, point.y());
    PaintBackground(textCanvas, block, start);
    PaintShadow(textCanvas, block, start);
    textCanvas->drawTextBlob(block.blob, start.x(), start.y(), paint);
  }

  PaintDecorations(textCanvas, line, point);
}

SkScalar SkParagraph::ComputeDecorationThickness(SkTextStyle textStyle) {

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

SkScalar SkParagraph::ComputeDecorationPosition(Block block, SkScalar thickness) {

  SkFontMetrics metrics;
  block.textStyle.getFontMetrics(metrics);

  SkScalar position;

  switch (block.textStyle.getDecoration()) {
    case SkTextDecoration::kUnderline:
      if (metrics.hasUnderlinePosition(&position)) {
        return position - metrics.fAscent;
      } else {
        position = metrics.fDescent - metrics.fAscent;
        if (block.textStyle.getDecorationStyle() == SkTextDecorationStyle::kWavy ||
            block.textStyle.getDecorationStyle() == SkTextDecorationStyle::kDouble
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
    case SkTextDecoration::kLineThrough:
      if (!metrics.hasStrikeoutPosition(&position)) {
        return position - metrics.fAscent;
      } else {
        position = (metrics.fDescent - metrics.fAscent) / 2;
      }
      break;
    default:
      position = 0;
      SkASSERT(false);
      break;
  }

  return position;
}

void SkParagraph::ComputeDecorationPaint(Block block, SkPaint& paint, SkPath& path, SkScalar width) {

  paint.setStyle(SkPaint::kStroke_Style);
  if (block.textStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(block.textStyle.getColor());
  } else {
    paint.setColor(block.textStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setTextSize(block.textStyle.getFontSize());
  paint.setTypeface(block.textStyle.getTypeface());

  SkScalar scaleFactor = block.textStyle.getFontSize() / 14.f;

  switch (block.textStyle.getDecorationStyle()) {
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

void SkParagraph::PaintDecorations(SkCanvas* canvas, const Line& line, SkPoint offset) {

  std::vector<Block>::const_iterator start = line.blocks.begin();
  SkScalar width = 0;
  for (auto block = line.blocks.begin(); block != line.blocks.end(); ++block) {
    if (start == block) {
      width += block->rect.width();
    } else if (start->textStyle == block->textStyle) {
      width += block->rect.width();
    } else {
      PaintDecorations(canvas, start, block, offset, width);
      start = block;
      width += block->rect.width();
    }
  }

  if (start != line.blocks.end()) {
    PaintDecorations(canvas, start, line.blocks.end(), offset, width);
  }
}

void SkParagraph::PaintDecorations(SkCanvas* canvas,
                                   std::vector<Block>::const_iterator begin,
                                   std::vector<Block>::const_iterator end,
                                   SkPoint offset,
                                   SkScalar width) {

  if (begin == end) {
    return;
  }

  auto block = *begin;
  if (block.textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  // Decoration thickness
  SkScalar thickness = ComputeDecorationThickness(block.textStyle);

  // Decoration position
  SkScalar position = ComputeDecorationPosition(block, thickness);

  // Decoration paint (for now) and/or path
  SkPaint paint;
  SkPath path;
  ComputeDecorationPaint(block, paint, path, width);
  paint.setStrokeWidth(thickness);

  // Draw the decoration
  SkScalar x = offset.x() + block.rect.left() + block.shift;
  SkScalar y = offset.y() + block.rect.top() + position;
  switch (block.textStyle.getDecorationStyle()) {
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

void SkParagraph::PaintBackground(SkCanvas* canvas, Block block, SkPoint offset) {

  if (!block.textStyle.hasBackground()) {
    return;
  }
  block.rect.offset(offset.fY, offset.fY);
  canvas->drawRect(block.rect, block.textStyle.getBackground());
}

void SkParagraph::PaintShadow(SkCanvas* canvas, Block block, SkPoint offset) {
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

  _lines.clear();
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

    int32_t status = breaker->preceding(firstChar);
    if (status == icu::BreakIterator::DONE) {
      // Take care of the first line
      firstChar = 0;
    } else {
      firstChar = status;
      if (breaker->getRuleStatus() != UBRK_LINE_HARD) {
        continue;
      }
    }

    // Remove all insignificant characters at the end of the line (whitespaces)
    // TODO: we keep at least one space in case the line is all spaces for now
    // TODO: since Flutter is using a space character to measure things;
    // TODO: need to fix it later
    while (lastChar > firstChar) {
      int32_t character = *(_text16.begin() + lastChar - 1);
      if (!u_isWhitespace(character)) {
        break;
      }
      lastChar -= 1;
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
    blocks.reserve(lastStyle - firstStyle);
    for (auto s = firstStyle; s < lastStyle; ++s) {
      auto& style = _styles[s];
      blocks.emplace_back(
          SkTMax(style.start, firstChar),
          SkTMin(style.end, lastChar),
          style.textStyle);
    }

    // Add one more string to the list
    _lines.emplace(_lines.begin(), blocks, breaker->getRuleStatus() == UBRK_LINE_HARD);

    // Move on
    lastChar = firstChar;
  }

  // Print all lines
  /*
  size_t linenum = 1;
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
        SkDebugf("Block:   %d:%d '%s'\n", start, end, str.c_str());
      }
    }
  }
  */
}

// TODO: implement properly (currently it only works as an indicator that something changed in the text)
std::vector<SkTextBox> SkParagraph::GetRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rect_height_style,
    RectWidthStyle rect_width_style) {
  std::vector<SkTextBox> result;
  for (auto& line : _lines) {
    if (line.End() <= start) {
      continue;
    } else if (line.Start() >= end) {
      break;
    }
    for (auto& block : line.blocks) {
      if (block.end <= start || block.start >= end) {
        continue;
      }
      result.emplace_back(block.rect, SkTextDirection::ltr);
    }
  }
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