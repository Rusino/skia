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

    SkLine(SkScalar width, SkSpan<SkWord> words)
    : SkLine() {
        fWords = words;
    }

    inline SkVector advance() { return fAdvance; }
    inline SkVector offset() { return fOffset; }

    void formatByWords(SkTextAlign align, SkScalar maxWidth);

    void paintByStyles(SkCanvas* canvas);

    void getRectsForRange(
        SkTextDirection textDirection,
        const char* start,
        const char* end,
        std::vector<SkTextBox>& result);

  private:

    SkScalar fShift;    // Shift to left - right - center
    SkVector fAdvance;
    SkVector fOffset;
    SkScalar fWidth;    // Could be different from advance because of formatting
    SkScalar fHeight;

    SkSpan<SkWord> fWords;
};