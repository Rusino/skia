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

  SkLine(SkScalar width, SkScalar height, SkArraySpan<SkWords> words, SkArraySpan<SkRun> runs);

  inline SkVector advance() const { return fAdvance; }

  void formatByWords(SkTextAlign align, SkScalar maxWidth);

  void paintText(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void paintBackground(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void paintShadow(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void paintDecorations(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void computeDecorationPaint(SkPaint& paint, SkRect clip, SkTextStyle style, SkPath& path) const;

  void iterateThroughRuns(
      SkSpan<const char> text,
      std::function<void(SkRun* run, int32_t pos, size_t size, SkRect clip)> apply) const;

  void getRectsForRange(
      SkTextDirection textDirection,
      const char* start,
      const char* end,
      std::vector<SkTextBox>& result);

 private:

  friend class SkSection;

  void justify(SkScalar delta);

  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;  // Text on the line size
  SkVector fOffset;
  SkScalar fWidth;    // Could be different from advance because of formatting
  SkScalar fHeight;
  SkScalar fBaseline;

  SkSpan<const char> fText;
  SkArraySpan<SkWords> fUnbreakableWords; // For flutter and for justification
  SkArraySpan<SkRun> fRuns;
  SkTArray<SkStyle> fStyledBlocks;
};