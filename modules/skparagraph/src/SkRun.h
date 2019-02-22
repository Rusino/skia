/*
 * Copyright 2019 Google Inc.
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
class SkRun
{
  public:

    SkRun() { }
    SkRun(
        const SkFont& font,
        const SkShaper::RunHandler::RunInfo& info,
        int glyphCount,
        SkSpan<const char> text);

    void endRunBuffer(SkScalar offset, SkScalar advance) { }

    SkShaper::RunHandler::Buffer newRunBuffer();

    inline size_t size() const { return fGlyphs.size(); }
    inline SkVector advance() { return fInfo.fAdvance; }
    inline SkScalar ascent() { return fInfo.fAscent; }
    inline SkScalar descent() { return fInfo.fDescent; }
    inline SkScalar leading() { return fInfo.fLeading; }

    inline SkSpan<const char> text() { return fText; }

  private:

    friend class SkWord;

    SkFont fFont;
    SkShaper::RunHandler::RunInfo fInfo;
    SkSTArray<128, SkGlyphID, true> fGlyphs;
    SkSTArray<128, SkPoint, true> fPositions;
    SkSTArray<128, uint32_t, true> fClusters;

    SkScalar fOffset;
    SkScalar fAdvance;

    SkSpan<const char> fText;
};