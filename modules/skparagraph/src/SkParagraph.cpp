/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "unicode/utypes.h"
#include "unicode/unistr.h"

#include "SkParagraph.h"
#include "SkCanvas.h"
#include "SkSize.h"

static void Callback(void* this_pointer,
                     int line_number,
                     SkScalar maxAscent,
                     SkScalar maxDescent,
                     SkScalar maxLeading,
                     int previousRunIndex,
                     int runIndex,
                     SkPoint point) {
  SkParagraph* self = static_cast<SkParagraph*>(this_pointer);
  self->LineBreakerCallback(line_number, maxAscent, maxDescent, maxLeading, previousRunIndex, runIndex, point);
}

SkParagraph::SkParagraph()
  : _shaper(nullptr) {
}

SkParagraph::~SkParagraph() = default;

double SkParagraph::GetMaxWidth() {
  return _width;
}

double SkParagraph::GetHeight() {
  return _height;
}

double SkParagraph::GetMinIntrinsicWidth() {
  return _minIntrinsicWidth;
}

double SkParagraph::GetMaxIntrinsicWidth() {
  return _maxIntrinsicWidth;
}

double SkParagraph::GetAlphabeticBaseline() {
  return _alphabeticBaseline;
}

double SkParagraph::GetIdeographicBaseline() {
  return _ideographicBaseline;
}

bool SkParagraph::DidExceedMaxLines() {
  return _linesNumber > _maxLines;
}

void SkParagraph::SetText(std::vector<uint16_t> utf16text) {

  _text16 = std::move(utf16text);
}

void SkParagraph::SetText(const char* utf8text, size_t textBytes) {
  _text = utf8text;
  _textLen = textBytes;
}

void SkParagraph::SetParagraphStyle(SkColor foreground,
                                    SkColor background,
                                    double fontSize,
                                    std::string fontFamily,
                                    bool fontBold,
                                    TextDirection dir,
                                    size_t maxLines) {
  _dir = dir;
  _maxLines = maxLines;
  _foreground = foreground;
  _background = background;
  _fontFamily = fontFamily;
  _fontSize = fontSize;
  _fontBold = fontBold;
}

void SkParagraph::Layout(double width) {
  if (Shape()) {
    BreakLines(width);
  }
}

bool SkParagraph::Shape() {

  _shaper.resetLayout();
  _shaper.resetLinebreaks();

  // TODO: change the shaper's text parameter
  const char* utf8 = nullptr;
  size_t utf8Len = 0;

  if (_textLen == 0) {
    auto utf16 = icu::UnicodeString(_text16.data(), _text16.size());
    std::string str;
    utf16.toUTF8String(str);
    utf8 = str.data();
    utf8Len = _text16.size();
  } else {
    utf8 = _text;
    utf8Len = _textLen;
  }

  // TODO: get rid of the paint
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setTextSize(_fontSize);
  paint.setTypeface(SkTypeface::MakeFromName(_fontFamily.data(), _fontBold ? SkFontStyle::Bold() : SkFontStyle()));

  SkFont font = SkFont::LEGACY_ExtractFromPaint(paint);
  if (!_shaper.generateGlyphs(font, utf8, utf8Len, _dir == TextDirection::ltr)) {
    return false;
  }

  return true;
}

void SkParagraph::BreakLines(double width) {

  _shaper.resetLinebreaks();
  // Iterate over the glyphs in logical order to mark line endings.
  _shaper.generateLineBreaks(width);

  // Reorder the runs and glyphs per line and write them out.
  auto size = _shaper.generateTextBlob(&_builder, {0, 0}, Callback, this);
  _height = size.fY;
  _width = width;
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) {

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setColor(_foreground);

  canvas->drawTextBlob(_builder.make(), x, y, paint);
}

void SkParagraph::Reset() {

  _shaper.resetLayout();
  _shaper.resetLinebreaks();

}

void SkParagraph::LineBreakerCallback(
              int line_number,
              SkScalar maxAscent,
              SkScalar maxDescent,
              SkScalar maxLeading,
              int previousRunIndex,
              int runIndex,
              SkPoint point) {
  _linesNumber = line_number;
}

std::vector<TextBox> SkParagraph::GetRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rect_height_style,
    RectWidthStyle rect_width_style) {
  // TODO: implement
  std::vector<TextBox> result;
  return result;
}

PositionWithAffinity SkParagraph::GetGlyphPositionAtCoordinate(double dx, double dy) const {
  // TODO: implement
  return PositionWithAffinity(0, Affinity::UPSTREAM);
}

Range<size_t> SkParagraph::GetWordBoundary(unsigned offset) {
  // TODO: implement
  Range<size_t> result;
  return result;
}
