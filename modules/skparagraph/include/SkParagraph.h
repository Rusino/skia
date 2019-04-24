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
#include "SkFontCollection.h"

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
  SkParagraph(const std::string& text, SkParagraphStyle style, sk_sp<SkFontCollection> fonts)
      : fFontCollection(std::move(fonts))
      , fParagraphStyle(style)
      , fUtf8(text.data(), text.size()) { }

  SkParagraph(const std::u16string& utf16text, SkParagraphStyle style, sk_sp<SkFontCollection> fonts);

  virtual ~SkParagraph() = default;

  SkScalar getMaxWidth() { return fWidth; }

  SkScalar getHeight() { return fHeight; }

  SkScalar getMinIntrinsicWidth() { return fMinIntrinsicWidth; }

  SkScalar getMaxIntrinsicWidth() { return fMaxIntrinsicWidth; }

  SkScalar getAlphabeticBaseline() { return fAlphabeticBaseline; }

  SkScalar getIdeographicBaseline() { return fIdeographicBaseline; }

  virtual bool didExceedMaxLines() = 0;

  virtual void layout(SkScalar width) = 0;

  virtual void paint(SkCanvas* canvas, SkScalar x, SkScalar y) = 0;

  // Returns a vector of bounding boxes that enclose all text between
  // start and end glyph indexes, including start and excluding end
  virtual std::vector<SkTextBox> getRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rectHeightStyle,
      RectWidthStyle rectWidthStyle) = 0;

  // Returns the index of the glyph that corresponds to the provided coordinate,
  // with the top left corner as the origin, and +y direction as down
  virtual SkPositionWithAffinity
  getGlyphPositionAtCoordinate(SkScalar dx, SkScalar dy) = 0;

  // Finds the first and last glyphs that define a word containing
  // the glyph at index offset
  virtual SkRange<size_t> getWordBoundary(unsigned offset) = 0;

 protected:

  friend class SkParagraphBuilder;

  sk_sp<SkFontCollection> fFontCollection;
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
