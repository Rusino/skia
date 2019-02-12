/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "uchar.h"
#include "SkColor.h"
#include "SkCanvas.h"
#include "SkFontMetrics.h"
#include "SkShaper.h"
#include "SkSpan.h"
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"
#include "SkTextBlobPriv.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "ShapedRun.h"
#include "ShapedLine.h"

// Comes from the paragraph
struct StyledText {

  StyledText() { }
  StyledText( SkSpan<const char> text, SkTextStyle textStyle)
      : text(text), textStyle(textStyle) { }

  bool operator==(const StyledText& rhs) const {
    return text.begin() == rhs.text.begin() &&
           text.end() == rhs.text.end() && // TODO: Can we have == on SkSpan?
           textStyle == rhs.textStyle;
  }
  SkSpan<const char> text;
  SkTextStyle textStyle;
};

class ShapedParagraph final : public SkShaper::RunHandler {
 public:

  ~ShapedParagraph() { }

  ShapedParagraph(SkParagraphStyle style, std::vector<StyledText> styles);

  // SkShaper::RunHandler interface
  SkShaper::RunHandler::Buffer newRunBuffer(const RunInfo& info, const SkFont& font, int glyphCount, SkSpan<const char> utf8) override
  {
    auto& word = _lines.back().addWord(font, info, glyphCount, utf8);
    return word.newRunBuffer();
  }

  void commitRun() override
  {
    auto& line = _lines.back();   // Last line
    auto& word = line.lastWord(); // Last word

    // Finish the word
    word.finish(line.advance());

    // Update the line stats
    line.update();

    // Update the paragraph stats
    _maxIntrinsicWidth = SkMaxScalar(_maxIntrinsicWidth, line.advance().fX);
    _minIntrinsicWidth = SkMaxScalar(_minIntrinsicWidth, word.advance().fX);
  }

  void commitLine() override
  {
    // Finish the line
    auto& line = _lines.back();
    line.finish();

    // Update the paragraph stats
    _height = line.advance().fY;
    _width = SkMaxScalar(_width, line.advance().fX);

    // Add the next line
    _lines.emplace_back();
  }

  void layout(SkScalar maxWidth, size_t maxLines);

  void printBlocks(size_t linenum);

  void format(SkScalar maxWidth);

  void paint(SkCanvas* textCanvas, SkPoint& point);

  SkScalar alphabeticBaseline() { return _alphabeticBaseline; }
  SkScalar height() { return _height; }
  SkScalar width() { return _width; }
  SkScalar ideographicBaseline() { return _ideographicBaseline; }
  SkScalar maxIntrinsicWidth() { return _maxIntrinsicWidth; }
  SkScalar minIntrinsicWidth() { return _minIntrinsicWidth; }

  void GetRectsForRange(const char* start, const char* end, std::vector<SkTextBox>& result);

  size_t lineNumber() const { return _lines.size(); }

 private:
  // Constrains
  size_t _maxLines;

  // Input
  SkParagraphStyle _style;
  std::vector<StyledText> _styles;

  // Output to Flutter
  size_t _linesNumber;
  SkScalar _alphabeticBaseline;
  SkScalar _ideographicBaseline;
  SkScalar _height;
  SkScalar _width;
  SkScalar _maxIntrinsicWidth;
  SkScalar _minIntrinsicWidth;

  // Internal structures
  bool     _exceededLimits;     // Lines number exceed the limit and there is an ellipse
  SkTArray<Line> _lines;        // All lines that the shaper produced
  // TODO: Shadows
};