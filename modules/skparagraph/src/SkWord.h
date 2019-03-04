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
#include "SkArraySpan.h"

// TODO: Use one handler for both the initial shaping and for words reshaping
class SkRun;
class SkWord {
  public:

    SkWord() { }

    SkWord(SkSpan<const char> text, SkSpan<const char> spaces, bool lineBreakBefore);
    SkWord(SkSpan<const char> text, SkArraySpan<SkRun> runs);

    void generate(SkVector offset);
    void update(SkArraySpan<SkRun> runs);

    SkSpan<const char> span() const { return SkSpan<const char>(fText.begin(), fText.size() + fSpaces.size()); }
    inline void shift(SkScalar shift) { fShift += shift; }
    inline void expand(SkScalar step) { fFullWidth += step; }
    inline void trim() { bTrimmed = true; }
    inline SkSpan<const char> text() { return fText; }
    inline SkVector fullAdvance()  { return SkVector::Make(fFullWidth, fHeight); }
    inline SkVector trimmedAdvance() { return SkVector::Make(fRightTrimmedWidth, fHeight); }
    inline SkVector offset() { return fOffset; }

    SkRect rect() { return SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fFullWidth, fHeight); }

    // Take in account possible many
    void paint(SkCanvas* canvas, SkScalar offsetX, SkScalar offsetY, SkSpan<StyledText> styles);

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

    SkSpan<StyledText> fTextStyles;
    SkSpan<const char> fText;
    SkSpan<const char> fSpaces;
    SkVector fOffset;
    SkScalar fHeight;
    SkScalar fFullWidth;
    SkScalar fRightTrimmedWidth;
    SkScalar fShift;// Caused by justify text alignment
    SkArraySpan<SkRun> fRuns;
    sk_sp<SkTextBlob> fBlob;
    size_t gLeft;   // Glyph index on the first run that starts the word
    size_t gRight;  // Glyph index on the last run that ends the word
    size_t gTrim;
    bool bTrimmed;
    bool fMayLineBreakBefore;
};