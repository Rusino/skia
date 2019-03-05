/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkDartTypes.h"
#include "SkCanvas.h"
#include "SkWord.h"

class SkLine {

 public:

  SkLine();

  SkLine(SkVector advance, SkScalar baseline, SkSpan<StyledText> styles, SkArraySpan<SkWord> words);

  inline SkVector advance() const { return fAdvance; }

  void formatByWords(SkTextAlign align, SkScalar maxWidth);

  void paintByStyles(SkCanvas* canvas);

  void getRectsForRange(
      SkTextDirection textDirection,
      const char* start,
      const char* end,
      std::vector<SkTextBox>& result);

 private:

  void generateWordTextBlobs(SkScalar offsetX);

  void paintText(SkCanvas* canvas);

  void paintBackground(SkCanvas* canvas);

  void paintShadow(SkCanvas* canvas);

  void paintDecorations(SkCanvas* canvas);

  void justify(SkScalar delta);

  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;
  SkScalar fWidth;    // Could be different from advance because of formatting
  SkScalar fHeight;
  SkScalar fBaseline;

  SkSpan<StyledText> fTextStyles;
  SkArraySpan<SkWord> fWords;
};