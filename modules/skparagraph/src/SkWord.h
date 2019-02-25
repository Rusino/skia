/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkCanvas.h"
#include "SkSpan.h"
#include "SkScalar.h"
#include "SkShaper.h"
#include "SkTextStyle.h"

// TODO: Use one handler for both the initial shaping and for words reshaping
class SkRun;
class SkWord {
  public:


    SkWord(SkSpan<const char> text, SkSpan<SkRun> runs);

    void generate(SkVector offset);

    inline void shift(SkScalar shift) { fShift += shift; }
    inline void expand(SkScalar step) { fAdvance.fX += step; }
    inline SkSpan<const char> text() { return fText; }
    inline SkVector advance() { return fAdvance; }
    inline SkVector offset() { return fOffset; }

    SkRect rect() { return SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY); }

    // Take in account possible many
    void paint(SkCanvas* canvas, SkPoint point, SkSpan<StyledText> styles);

  private:

    SkVector getAdvance(const SkRun& run, size_t start, size_t end);
    SkVector getOffset(const SkRun& run, size_t start);

    void paintShadow(SkCanvas* canvas);
    void paintBackground(SkCanvas* canvas, SkPoint point);
    void paintDecorations(SkCanvas* canvas);
    SkScalar computeDecorationThickness(SkTextStyle textStyle);
    SkScalar computeDecorationPosition(SkScalar thickness);
    void computeDecorationPaint(SkPaint& paint, SkPath& path);

    friend class SkSection;

    SkSpan<StyledText> fStyles;
    SkSpan<const char> fText;
    SkVector fOffset;
    SkVector fAdvance;
    SkScalar fShift;// Caused by justify text alignment
    SkSpan<SkRun> fRuns;
    sk_sp<SkTextBlob> fBlob;
    size_t fLeft;
    size_t fRight;

};