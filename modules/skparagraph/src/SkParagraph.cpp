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

SkParagraph::SkParagraph()
    : _shaper(nullptr) {
}

SkParagraph::~SkParagraph() = default;

double SkParagraph::GetMaxWidth() {
  return SkScalarToDouble(_width);
}

double SkParagraph::GetHeight() {
  return SkScalarToDouble(_height);
}

double SkParagraph::GetMinIntrinsicWidth() {
  // TODO: return _minIntrinsicWidth;
  return SkScalarToDouble(_width);
}

double SkParagraph::GetMaxIntrinsicWidth() {
  // TODO: return _maxIntrinsicWidth;
  return SkScalarToDouble(_width);
}

double SkParagraph::GetAlphabeticBaseline() {
  return SkScalarToDouble(_alphabeticBaseline);
}

double SkParagraph::GetIdeographicBaseline() {
  return SkScalarToDouble(_ideographicBaseline);
}

bool SkParagraph::DidExceedMaxLines() {
  return _linesNumber > _maxLines;
}

void SkParagraph::SetText(std::vector<uint16_t> utf16text) {

  icu::UnicodeString utf16 = icu::UnicodeString(utf16text.data(), utf16text.size());
  std::string str;
  utf16.toUTF8String(str);

  _textLen = utf16text.size();
  _text = new char[_textLen];
  strncpy(_text, str.c_str(), _textLen);
}

void SkParagraph::SetText(const char* utf8text, size_t textBytes) {

  _textLen = textBytes;
  _text = new char[_textLen];
  strncpy(_text, utf8text, textBytes);
}

void SkParagraph::SetParagraphStyle(SkColor foreground,
                                    SkColor background,
                                    double fontSize,
                                    const std::string& fontFamily,
                                    bool fontBold,
                                    TextDirection dir,
                                    size_t maxLines) {
  _dir = dir;
  _maxLines = maxLines;
  _foreground = foreground;
  _background = background;
  _fontFamily = fontFamily;
  _fontSize = SkDoubleToScalar(fontSize);
  _fontBold = fontBold;
}

void SkParagraph::Layout(double width) {

  _alphabeticBaseline = 0;
  _height = 0;
  _width = 0;
  _ideographicBaseline = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = std::numeric_limits<float>::max();
  _linesNumber = 0;

  if (Shape()) {
    BreakLines(width);
  }
}

bool SkParagraph::Shape() {

  _shaper.resetLayout();
  _shaper.resetLinebreaks();

  // TODO: get rid of the paint
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setTextSize(_fontSize);
  paint.setTypeface(SkTypeface::MakeFromName(_fontFamily.data(), _fontBold ? SkFontStyle::Bold() : SkFontStyle()));

  SkFont font = SkFont::LEGACY_ExtractFromPaint(paint);
  if (!_shaper.generateGlyphs(font, _text, _textLen, _dir == TextDirection::ltr)) {
    SkDebugf("Error shaping\n");
    return false;
  }

  return true;
}

void SkParagraph::BreakLines(double width) {

  _shaper.resetLinebreaks();
  // Iterate over the glyphs in logical order to mark line endings.
  _shaper.generateLineBreaks(SkDoubleToScalar(width));

  // Reorder the runs and glyphs per line and write them out.
  _shaper.generateTextBlob(&_builder, {0, 0}, [this](size_t line_number,
                                                     SkScalar maxAscent,
                                                     SkScalar maxDescent,
                                                     SkScalar maxLeading,
                                                     int previousRunIndex,
                                                     int runIndex,
                                                     SkPoint point) {
    //SkDebugf("Line break1 %d (%g, %g)\n", line_number, point.fX, point.fY);
    this->_linesNumber = line_number;
    if (this->_height < point.fY) {
      this->_height = point.fY;
    }
    if (this->_width < point.fX) {
      this->_width = point.fX;
    }
  });
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) {

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setColor(_foreground);

  canvas->drawTextBlob(_builder.make(), SkDoubleToScalar(x), SkDoubleToScalar(y), paint);
}

void SkParagraph::Reset() {

  _shaper.resetLayout();
  _shaper.resetLinebreaks();

}

std::vector<TextBox> SkParagraph::GetRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rect_height_style,
    RectWidthStyle rect_width_style) {
  // TODO: implement
  SkASSERT(false);
  std::vector<TextBox> result;
  return result;
}

PositionWithAffinity SkParagraph::GetGlyphPositionAtCoordinate(double dx, double dy) const {
  // TODO: implement
  SkASSERT(false);
  return PositionWithAffinity(0, Affinity::UPSTREAM);
}

Range<size_t> SkParagraph::GetWordBoundary(unsigned offset) {
  // TODO: implement
  SkASSERT(false);
  Range<size_t> result;
  return result;
}
