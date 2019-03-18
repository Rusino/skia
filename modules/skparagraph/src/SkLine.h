/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkDartTypes.h"
#include "SkSpan.h"

class SkLine {

 public:

  SkLine();

  SkLine(SkVector offset, SkVector advance, SkSpan<const char> text);

  inline SkSpan<const char> text() const { return fText; }
  inline SkVector advance() const { return fAdvance; }
  inline SkVector offset() const { return fOffset + SkVector::Make(fShift, 0); }
  inline bool empty() const { return fText.empty(); }

  void formatByWords(SkTextAlign align, SkScalar maxWidth);

 private:

  void justify(SkScalar delta);

  SkSpan<const char> fText;
  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;  // Text on the line size
  SkVector fOffset;
};