/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"
#include "../../src/ShapedParagraph.h"

struct Block {
  Block(size_t start, size_t end, SkTextStyle style)
      : start(start), end(end), textStyle(style) { }
  size_t start;
  size_t end;
  SkTextStyle textStyle;
};

class SkCanvas;
class SkParagraph {
 public:
  SkParagraph(const std::u16string& utf16text,
              SkParagraphStyle style,
              std::vector<Block> blocks);

  SkParagraph(const std::string& utf8text,
              SkParagraphStyle style,
              std::vector<Block> blocks);

  ~SkParagraph();

  double GetMaxWidth();

  double GetHeight();

  double GetMinIntrinsicWidth();

  double GetMaxIntrinsicWidth();

  double GetAlphabeticBaseline();

  double GetIdeographicBaseline();

  bool DidExceedMaxLines() {
    return !_style.unlimited_lines() && _linesNumber > _style.getMaxLines();
  }

  bool Layout(double width);

  void Paint(SkCanvas* canvas, double x, double y) const;

  std::vector<SkTextBox> GetRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rect_height_style,
      RectWidthStyle rect_width_style);

  SkPositionWithAffinity GetGlyphPositionAtCoordinate(double dx, double dy) const;

  SkRange<size_t> GetWordBoundary(unsigned offset);

 private:

  friend class ParagraphTester;

  // Record a picture drawing all small text blobs
  void RecordPicture();

  // Break the text by explicit line breaks
  void BreakTextIntoParagraphs();

  // Things for Flutter
  SkScalar _alphabeticBaseline;
  SkScalar _ideographicBaseline;
  SkScalar _height;
  SkScalar _width;
  SkScalar _maxIntrinsicWidth;
  SkScalar _minIntrinsicWidth;
  size_t   _linesNumber;

  // Input
  SkParagraphStyle _style;
  std::vector<StyledText> _styles;
  SkSpan<const char> _utf8;

  // Shaping (list of paragraphs in Shaper terms separated by hard line breaks)
  std::vector<ShapedParagraph> _paragraphs;

  // Painting
  sk_sp<SkPicture> _picture;
};