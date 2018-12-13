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

SkParagraph::SkParagraph(sk_sp<SkTypeface> typeface)
    : _shaper(typeface) {
}

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
  return _linesNumber > _maxLines;
}

void SkParagraph::SetText(std::vector<uint16_t> utf16text) {
/*
  icu::UnicodeString utf16 = icu::UnicodeString(utf16text.data(), utf16text.size());
  std::string str;
  utf16.toUTF8String(str);
  SkDebugf("Draw paragraph: %s", str.c_str());
*/
  _textLen = utf16text.size();
  _text16 = new uint16_t[_textLen + 1];
  memcpy(_text16, utf16text.data(), _textLen * sizeof(uint16_t));
}

void SkParagraph::SetText(const char* utf8text, size_t textBytes) {

  icu::UnicodeString utf16 = icu::UnicodeString::fromUTF8(icu::StringPiece(utf8text, textBytes));
  std::string str;
  utf16.toUTF8String(str);

  _textLen = textBytes;
  _text16 = new uint16_t[_textLen + 1];
  memcpy(_text16, utf16.getBuffer(), _textLen * sizeof(uint16_t));
}

void SkParagraph::SetParagraphStyle(SkColor foreground,
                                    SkColor background,
                                    double fontSize,
                                    const std::string& fontFamily,
                                    SkFontStyle::Weight weight,
                                    TextDirection dir,
                                    size_t maxLines) {
  _dir = dir;
  _maxLines = maxLines;
  _foreground = foreground;
  _background = background;
  _fontFamily = fontFamily;
  _fontSize = SkDoubleToScalar(fontSize);
  _weight = weight;
}

void SkParagraph::Layout(double width) {

  _alphabeticBaseline = 0;
  _height = 0;
  _width = 0;
  _ideographicBaseline = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
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
  paint.setTypeface(SkTypeface::MakeFromName(_fontFamily.data(), SkFontStyle(_weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant)));

  SkFont font = SkFont::LEGACY_ExtractFromPaint(paint);

  if (!_shaper.generateGlyphs(font, _text16, _textLen, _dir == TextDirection::ltr)) {
    SkDebugf("Error shaping\n");
    return false;
  }

  return true;
}

void SkParagraph::BreakLines(double width) {
  /*
  icu::UnicodeString utf16 = icu::UnicodeString(_text16, _textLen);
  std::string str;
  utf16.toUTF8String(str);
  SkDebugf("Generate textblob: %s\n", str.c_str());
  */

  _shaper.resetLinebreaks();
  // Iterate over the glyphs in logical order to mark line endings.
  bool breakable = _shaper.generateLineBreaks(SkDoubleToScalar(width));

  // Reorder the runs and glyphs per line and write them out.
  SkTextBlobBuilder builder;
  _shaper.generateTextBlob(&builder, {0, 0}, [this](size_t line_number,
                                                     SkSize size,
                                                     int previousRunIndex,
                                                     int runIndex) {
    //SkDebugf("Line break1 %d (%g, %g)\n", line_number, point.fX, point.fY);
    _linesNumber = line_number;
    _height = SkMaxScalar(_height, size.fHeight);
    _width = SkMaxScalar(_width, size.fWidth);
    _maxIntrinsicWidth += size.fWidth;
  });

  if (breakable) {
    _shaper.breakIntoWords([this](SkSize size, int startIndex, int nextStartIndex) {
      _minIntrinsicWidth = SkMaxScalar(_minIntrinsicWidth, size.fWidth);
    });
  }

  _blob = builder.make();
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) {
/*
  icu::UnicodeString utf16 = icu::UnicodeString(_text16, _textLen);
  std::string str;
  utf16.toUTF8String(str);
  SkDebugf("Draw paragraph: %s\n", str.c_str());
*/
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setLCDRenderText(true);
  paint.setColor(_foreground);

  canvas->drawTextBlob(_blob, SkDoubleToScalar(x), SkDoubleToScalar(y), paint);
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
  //SkASSERT(false);
  return PositionWithAffinity(0, Affinity::UPSTREAM);
}

Range<size_t> SkParagraph::GetWordBoundary(unsigned offset) {
  // TODO: implement
  SkASSERT(false);
  Range<size_t> result;
  return result;
}
