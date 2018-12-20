/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkRect.h"

enum Affinity { UPSTREAM, DOWNSTREAM };

enum class RectHeightStyle {
  // Provide tight bounding boxes that fit heights per run.
      kTight,

  // The height of the boxes will be the maximum height of all runs in the
  // line. All rects in the same line will be the same height.
      kMax,

  // Extends the top and/or bottom edge of the bounds to fully cover any line
  // spacing. The top edge of each line should be the same as the bottom edge
  // of the line above. There should be no gaps in vertical coverage given any
  // ParagraphStyle line_height.
  //
  // The top and bottom of each rect will cover half of the
  // space above and half of the space below the line.
      kIncludeLineSpacingMiddle,
  // The line spacing will be added to the top of the rect.
      kIncludeLineSpacingTop,
  // The line spacing will be added to the bottom of the rect.
      kIncludeLineSpacingBottom
};

enum class RectWidthStyle {
  // Provide tight bounding boxes that fit widths to the runs of each line
  // independently.
      kTight,

  // Extends the width of the last rect of each line to match the position of
  // the widest rect over all the lines.
      kMax
};

enum class SkTextAlign {
  left,
  right,
  center,
  justify,
  start,
  end,
};

enum class SkTextDirection {
  rtl,
  ltr,
};

struct PositionWithAffinity {
  const size_t position;
  const Affinity affinity;

  PositionWithAffinity(size_t p, Affinity a) : position(p), affinity(a) {}
};

struct TextBox {
  SkRect rect;
  SkTextDirection direction;

  TextBox(SkRect r, SkTextDirection d) : rect(r), direction(d) {}
};

template <typename T>
struct Range {
  Range() : start(), end() {}
  Range(T s, T e) : start(s), end(e) {}

  T start, end;

  bool operator==(const Range<T>& other) const {
    return start == other.start && end == other.end;
  }

  T width() { return end - start; }

  void Shift(T delta) {
    start += delta;
    end += delta;
  }
};

// Multiple decorations can be applied at once. Ex: Underline and overline is
// (0x1 | 0x2)
enum SkTextDecoration {
  kNone = 0x0,
  kUnderline = 0x1,
  kOverline = 0x2,
  kLineThrough = 0x4,
};

enum SkTextDecorationStyle { kSolid, kDouble, kDotted, kDashed, kWavy };
