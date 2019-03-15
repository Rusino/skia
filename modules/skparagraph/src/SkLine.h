/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkDartTypes.h"
#include "SkCanvas.h"
#include "SkBlock.h"

class SkLine : public SkBlock {

 public:

  SkLine();

  SkLine(SkVector offset, SkVector advance, SkArraySpan<SkWords> words);

  inline SkVector advance() const { return fAdvance; }
  inline SkVector offset() const { return fOffset + SkVector::Make(fShift, 0); }
  inline bool empty() const { return fText.empty(); }

  void formatByWords(SkTextAlign align, SkScalar maxWidth);

  void getRectsForRange(
      SkTextDirection textDirection,
      const char* start,
      const char* end,
      std::vector<SkTextBox>& result);

 private:

  void justify(SkScalar delta);

  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;  // Text on the line size
  SkVector fOffset;

  SkArraySpan<SkWords> fUnbreakableWords; // For flutter and for justification
};