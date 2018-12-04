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
  SkDebugf("GetMaxWidth:%d\n", _width);
  return _width;
}

double SkParagraph::GetHeight() {
  SkDebugf("GetHeight:%d\n", _height);
  return _height;
}

double SkParagraph::GetMinIntrinsicWidth() {
  SkDebugf("GetMinIntrinsicWidth:%d\n", _minIntrinsicWidth);
  // TODO: return _minIntrinsicWidth;
  return _width;
}

double SkParagraph::GetMaxIntrinsicWidth() {
  SkDebugf("GetMaxIntrinsicWidth:%d\n", _maxIntrinsicWidth);
  // TODO: return _maxIntrinsicWidth;
  return _width;
}

double SkParagraph::GetAlphabeticBaseline() {
  SkDebugf("GetAlphabeticBaseline:%d\n", _alphabeticBaseline);
  return _alphabeticBaseline;
}

double SkParagraph::GetIdeographicBaseline() {
  SkDebugf("GetIdeographicBaseline:%d\n", _ideographicBaseline);
  return _ideographicBaseline;
}

bool SkParagraph::DidExceedMaxLines() {
  SkDebugf("DidExceedMaxLines:%d > %d\n", _linesNumber, _maxLines);
  return _linesNumber > _maxLines;
}

void SkParagraph::SetText(std::vector<uint16_t> utf16text) {

  icu::UnicodeString utf16 = icu::UnicodeString(utf16text.data(), utf16text.size());
  std::string str;
  utf16.toUTF8String(str);

  _textLen = utf16text.size();
  _text = new char[_textLen];
  strncpy(_text, str.c_str(), _textLen);
  SkDebugf("Text1:'%s' (%d)\n", _text, _textLen);
}

void SkParagraph::SetText(const char* utf8text, size_t textBytes) {

  _textLen = textBytes;
  _text = new char[_textLen];
  strncpy(_text, utf8text, textBytes);
  SkDebugf("Text2:'%s' (%d)\n", _text, _textLen);
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

  SkDebugf("Layout\n");
  _alphabeticBaseline = 0;
  _height = 0;
  _width = 0;
  _ideographicBaseline = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = std::numeric_limits<double>::max();
  _linesNumber = 0;

  if (Shape()) {
    BreakLines(width);
  }
}

bool SkParagraph::Shape() {

  SkDebugf("Shaping\n");
  SkDebugf("[0]: %g, %g\n", _width, _height);
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
    SkDebugf("[1]: %g, %g\n", _width, _height);
    return false;
  }

  SkDebugf("[2]: %g, %g\n", _width, _height);
  return true;
}

void SkParagraph::BreakLines(double width) {

  SkDebugf("Breaking\n");
  _shaper.resetLinebreaks();
  // Iterate over the glyphs in logical order to mark line endings.
  _shaper.generateLineBreaks(width);

  // Reorder the runs and glyphs per line and write them out.
  _shaper.generateTextBlob(&_builder, {0, 0}, [this](int line_number,
      SkScalar maxAscent,
      SkScalar maxDescent,
      SkScalar maxLeading,
      int previousRunIndex,
      int runIndex,
      SkPoint point) {
        SkDebugf("Line break %d (%g, %g)\n", line_number, point.fX, point.fY);
        _linesNumber = line_number;
        if (_height < point.fY) {
          _height = point.fY;
        }
        if (_width < point.fX) {
          _width = point.fX;
        }
        SkDebugf("[3]: %g, %g\n", _width, _height);
  });
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) {

  SkDebugf("Painting\n");
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setColor(_foreground);

  canvas->drawTextBlob(_builder.make(), x, y, paint);
}

void SkParagraph::Reset() {

  SkDebugf("Reset\n");
  _shaper.resetLayout();
  _shaper.resetLinebreaks();

}

std::vector<TextBox> SkParagraph::GetRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rect_height_style,
    RectWidthStyle rect_width_style) {
  // TODO: implement
  SkDebugf("GetRectsForRange\n");
  std::vector<TextBox> result;
  return result;
}

PositionWithAffinity SkParagraph::GetGlyphPositionAtCoordinate(double dx, double dy) const {
  // TODO: implement
  SkDebugf("GetGlyphPositionAtCoordinate\n");
  return PositionWithAffinity(0, Affinity::UPSTREAM);
}

Range<size_t> SkParagraph::GetWordBoundary(unsigned offset) {
  // TODO: implement
  SkDebugf("GetWordBoundary\n");
  Range<size_t> result;
  return result;
}
