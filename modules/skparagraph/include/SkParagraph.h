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
#include "SkTLazy.h"
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"
#include "SkFontCollection.h"


// Comes as a result of shaping (broken down by lines and styles)
struct StyledRun {
  StyledRun(size_t start, size_t end, sk_sp<SkTextBlob> blob, SkRect rect, SkTextStyle style)
      : start(start)
      , end(end)
      , blob(blob)
      , rect(rect)
      , textStyle(style) {}
  size_t start;
  size_t end;
  sk_sp<SkTextBlob> blob;
  SkRect rect;
  SkTextStyle textStyle;
};

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

  std::vector<uint16_t>& getText() { return _text16; }

  bool Layout(double width);

  void Paint(SkCanvas* canvas, double x, double y) const;

  std::vector<SkTextBox> GetRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rect_height_style,
      RectWidthStyle rect_width_style);

  SkPositionWithAffinity GetGlyphPositionAtCoordinate(double dx, double dy) const;

  SkRange<size_t> GetWordBoundary(unsigned offset);

  void SetText(std::vector<uint16_t> utf16text);
  void SetText(const char* utf8text, size_t textBytes);

  void SetStyles(std::vector<StyledText> styles);
  void SetParagraphStyle(SkParagraphStyle style);
  void SetFontCollection(std::shared_ptr<SkFontCollection> fontCollection) {
    _fontCollection = fontCollection;
  }

 private:

  void RecordPicture();

  // Creates and draws the decorations onto the canvas.
  void PaintDecorations(SkCanvas* canvas, StyledRun run, SkPoint offset);

  // Draws the background onto the canvas.
  void PaintBackground(SkCanvas* canvas, StyledRun run, SkPoint offset);

  // Draws the shadows onto the canvas.
  void PaintShadow(SkCanvas* canvas, StyledRun run, SkPoint offset);

  SkScalar _alphabeticBaseline;
  SkScalar _height;
  SkScalar _width;
  SkScalar _ideographicBaseline;
  SkScalar _maxIntrinsicWidth;
  SkScalar _minIntrinsicWidth;
  size_t   _linesNumber;

  // Input
  std::vector<uint16_t> _text16;
  std::shared_ptr<SkFontCollection> _fontCollection;
  std::vector<StyledText> _styles;
  // Shaping
  SkParagraphStyle _style;
  std::vector<StyledRun> _styledRuns;
  // Painting
  sk_sp<SkPicture> _picture;
};