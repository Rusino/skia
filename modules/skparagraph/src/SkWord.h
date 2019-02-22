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

    SkWord(SkSpan<const char> text, SkRun& run);

    SkWord(SkSpan<const char> wordSpan, SkRun* begin, SkRun* end);

    inline void shift(SkScalar shift) { fShift += shift; }
    inline void expand(SkScalar step) { fAdvance.fX += step; }
    inline SkSpan<const char> text() { return fText; }
    inline SkVector advance() { return fAdvance; }
    inline SkVector offset() { return fOffset; }

    SkRect rect() { return SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY); }

    // Take in account possible many
    void paint(SkCanvas* canvas);

  private:

    friend class SkSection;

    SkSpan<StyledText> fStyles;
    SkVector fOffset;
    SkVector fAdvance;
    SkScalar fShift;              // Caused by justify text alignment

    SkSpan<const char> fText;
    sk_sp<SkTextBlob> fBlob;

};