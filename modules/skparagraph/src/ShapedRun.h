/*
 * Copyright 2011 Google Inc.
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

// The smallest part of the text that is painted separately
class ShapedRun {

 public:
  ShapedRun(const SkFont& font,
            const SkShaper::RunHandler::RunInfo& info,
            int glyphCount,
            SkSpan<const char> text);

  SkVector finish(SkVector advance);

  SkShaper::RunHandler::Buffer newRunBuffer();

  void PaintShadow(SkCanvas* canvas, SkPoint offset);

  void PaintBackground(SkCanvas* canvas, SkPoint offset);

  SkScalar ComputeDecorationThickness(SkTextStyle textStyle);

  SkScalar ComputeDecorationPosition(SkScalar thickness);

  void ComputeDecorationPaint(SkPaint& paint, SkPath& path, SkScalar width);

  void PaintDecorations(SkCanvas* canvas, SkPoint offset, SkScalar width);

  void Paint(SkCanvas* canvas, SkTextStyle style, SkPoint& point);

  size_t size() const {
    SkASSERT(fGlyphs.size() == fPositions.size());
    return fGlyphs.size();
  }

  inline SkVector advance() { return fInfo.fAdvance; }
  inline SkScalar ascent() { return fInfo.fAscent; }
  inline SkScalar descent() { return fInfo.fDescent; }
  inline SkScalar leading() { return fInfo.fLeading; }

  inline SkSpan<const char> text() { return fText; }
  inline SkRect rect() { return fRect; }
  inline sk_sp<SkTextBlob> blob() { return fBlob; }

  void shift(SkScalar s) { fShift += s; }
  void expand(SkScalar s) { fRect.fRight += s; }

 private:
  SkFont fFont;
  SkShaper::RunHandler::RunInfo   fInfo;
  SkSTArray<128, SkGlyphID, true> fGlyphs;
  SkSTArray<128, SkPoint  , true> fPositions;

  SkSpan<const char> fText;
  SkTextStyle textStyle;
  sk_sp<SkTextBlob> fBlob;
  SkRect fRect;
  SkScalar fShift;
};