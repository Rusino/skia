/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"

class SkCanvas;

class SkParagraph {
 protected:
  struct Block {
    Block(size_t start, size_t end, SkTextStyle style)
        : fStart(start), fEnd(end), fStyle(style) {}
    size_t fStart;
    size_t fEnd;
    SkTextStyle fStyle;
  };

 public:
  SkParagraph(const std::string& text,
              SkParagraphStyle style,
              std::vector<Block> blocks)
      : fParagraphStyle(style)
      , fUtf8(text.data(), text.size()) { }

  SkParagraph(const std::u16string& utf16text,
              SkParagraphStyle style,
              std::vector<Block> blocks)
      : fParagraphStyle(style) {
    icu::UnicodeString
        unicode((UChar*) utf16text.data(), SkToS32(utf16text.size()));
    std::string str;
    unicode.toUTF8String(str);
    fUtf8 = SkSpan<const char>(str.data(), str.size());
  }

  virtual ~SkParagraph() = default;

  double getMaxWidth() { return SkScalarToDouble(fWidth); }

  double getHeight() { return SkScalarToDouble(fHeight); }

  double getMinIntrinsicWidth() { return SkScalarToDouble(fMinIntrinsicWidth); }

  double getMaxIntrinsicWidth() { return SkScalarToDouble(fMaxIntrinsicWidth); }

  double getAlphabeticBaseline() { return SkScalarToDouble(fAlphabeticBaseline); }

  double getIdeographicBaseline() { return SkScalarToDouble(fIdeographicBaseline); }

  virtual bool didExceedMaxLines() = 0;

  virtual bool layout(double width) = 0;

  virtual void paint(SkCanvas* canvas, double x, double y) = 0;

  virtual std::vector<SkTextBox> getRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rectHeightStyle,
      RectWidthStyle rectWidthStyle) = 0;

  virtual SkPositionWithAffinity
  getGlyphPositionAtCoordinate(double dx, double dy) const = 0;

  virtual SkRange<size_t> getWordBoundary(unsigned offset) = 0;

 protected:

  friend class SkParagraphBuilder;

  SkParagraphStyle fParagraphStyle;
  SkSpan<const char> fUtf8;

  // Things for Flutter
  SkScalar fAlphabeticBaseline;
  SkScalar fIdeographicBaseline;
  SkScalar fHeight;
  SkScalar fWidth;
  SkScalar fMaxIntrinsicWidth;
  SkScalar fMinIntrinsicWidth;
  SkScalar fMaxLineWidth;
};