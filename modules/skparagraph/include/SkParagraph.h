/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "SkColor.h"
#include "SkDartTypes.h"
#include "SkShaper.h"
#include "SkTextBlob.h"

class SkCanvas;
class SkParagraph {
 public:
  SkParagraph();
  ~SkParagraph();

  double GetMaxWidth();

  double GetHeight();

  double GetMinIntrinsicWidth();

  double GetMaxIntrinsicWidth();

  double GetAlphabeticBaseline();

  double GetIdeographicBaseline();

  bool DidExceedMaxLines();

  void Layout(double width);

  bool Shape();

  void BreakLines(double width);

  void Paint(SkCanvas* canvas, double x, double y);

  std::vector<TextBox> GetRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rect_height_style,
      RectWidthStyle rect_width_style);

  PositionWithAffinity GetGlyphPositionAtCoordinate(double dx, double dy) const;

  Range<size_t> GetWordBoundary(unsigned offset);

  void SetText(std::vector<uint16_t> utf16text);
  void SetText(const char* utf8text, size_t textBytes);

  void Reset();

  void SetParagraphStyle(SkColor foreground = SK_ColorBLACK,
                         SkColor background = SK_ColorWHITE,
                         double fontSize = 14.0,
                         const std::string& fontFamily = "",
                         bool fontBold = false,
                         TextDirection dir = TextDirection::ltr,
                         size_t maxLines = std::numeric_limits<size_t>::max());

 private:
  SkScalar _alphabeticBaseline;
  SkScalar _height;
  SkScalar _width;
  SkScalar _ideographicBaseline;
  SkScalar _maxIntrinsicWidth;
  SkScalar _minIntrinsicWidth;
  size_t   _linesNumber;

  SkShaper _shaper;

  uint16_t* _text16;
  //char* _text8;
  size_t _textLen;
  TextDirection _dir;
  size_t _maxLines;
  SkColor _foreground;
  SkColor _background;
  std::string _fontFamily;
  SkScalar _fontSize;
  bool _fontBold;

  SkTextBlobBuilder _builder;
};