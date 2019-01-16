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

struct Line {
  Line(std::vector<Block> blocks, bool hardBreak)
      : blocks(std::move(blocks))
      , hardBreak(hardBreak){
    size.fHeight = 0;
    size.fWidth = 0;
    spacer = 0;
    hardBreak = false;
  }
  std::vector<Block> blocks;
  SkSize size;
  SkScalar spacer;
  bool hardBreak;
  size_t Start() const { return blocks.empty() ? 0 : blocks.front().start; };
  size_t End() const { return blocks.empty() ? 0 : blocks.back().end; };
  size_t Length() const { return blocks.empty() ? 0 :  blocks.back().end - blocks.front().start; }
  bool IsEmpty() const { return blocks.empty() || Length() == 0; }
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

  void Runs(std::vector<StyledText> styles);
  void SetParagraphStyle(SkParagraphStyle style);

  void SetFontCollection(std::shared_ptr<SkFontCollection> fontCollection) {
    _fontCollection = fontCollection;
  }

 private:

  friend class ParagraphBuilderTester;

  void RecordPicture();

  // Creates and draws the decorations onto the canvas.
  void PaintDecorations(SkCanvas* canvas, Block block, SkPoint offset) const;

  // Draws the background onto the canvas.
  void PaintBackground(SkCanvas* canvas, Block block, SkPoint offset) const;

  // Draws the shadows onto the canvas.
  void PaintShadow(SkCanvas* canvas, Block block, SkPoint offset) const;

  // Break the text by explicit line breaks
  void BreakLines();

  // Layout one line without explicit line breaks
  bool LayoutLine(std::vector<Line>::iterator& iter, SkScalar width);

  // Paint one line (produced with explicit line break or shaper)
  void PaintLine(SkCanvas* textCanvas, SkPoint point, const Line& line) const;

  SkScalar _alphabeticBaseline;
  SkScalar _height;
  SkScalar _width;
  SkScalar _ideographicBaseline;
  SkScalar _maxIntrinsicWidth;
  SkScalar _minIntrinsicWidth;
  size_t   _linesNumber;

  // Input
  std::vector<uint16_t> _text16;
  std::vector<StyledText> _styles;
  std::shared_ptr<SkFontCollection> _fontCollection;
  // Shaping
  SkParagraphStyle _style;
  std::vector<Line> _lines;
  // Painting
  sk_sp<SkPicture> _picture;
};