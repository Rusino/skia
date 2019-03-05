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

  SkLine(SkVector advance, SkScalar baseline, SkArraySpan<SkWord> words);

  inline SkVector advance() { return fAdvance; }
  inline SkArraySpan<SkWord>& words() { return fWords; }

  void generateWords();

  void formatByWords(SkTextAlign align, SkScalar maxWidth);

  void paintByStyles(SkCanvas* canvas,
                     SkSpan<StyledText> fTextStyles);

  void generateWordTextBlobs(SkScalar offsetX, SkSpan<StyledText> fTextStyles);

  void paintText(SkCanvas* canvas);

  void paintBackground(SkCanvas* canvas);

  void paintShadow(SkCanvas* canvas);

  void paintDecorations(SkCanvas* canvas);

  void getRectsForRange(
      SkTextDirection textDirection,
      const char* start,
      const char* end,
      std::vector<SkTextBox>& result);

 private:

  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;
  SkScalar fWidth;    // Could be different from advance because of formatting
  SkScalar fHeight;
  SkScalar fBaseline;

  SkArraySpan<SkWord> fWords;
};