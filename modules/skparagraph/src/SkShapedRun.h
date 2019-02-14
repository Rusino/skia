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
class SkShapedRun
{

  public:
    SkShapedRun(const SkFont& font,
              const SkShaper::RunHandler::RunInfo& info,
              int glyphCount,
              SkSpan<const char> text);

    SkShapedRun(sk_sp<SkTextBlob> blob, SkRect rect, SkScalar shift, const SkTextStyle& style) {
        fBlob = blob;
        fRect = rect;
        fShift = shift;
        fStyle = style;
    }

    void finish(SkVector advance, SkScalar width);

    std::unique_ptr<SkShapedRun> applyStyle(SkSpan<const char> limit, const SkTextStyle& style);

    SkShaper::RunHandler::Buffer newRunBuffer();

    void Paint(
        SkCanvas* canvas,
        std::vector<StyledText>::iterator begin,
        std::vector<StyledText>::iterator end);

    size_t size() const
    {
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

    inline void shift(SkScalar s) { fShift += s; }
    inline void expand(SkScalar s) { fRect.fRight += s; }

  private:

    void paintShadow(SkCanvas* canvas);
    void paintBackground(SkCanvas* canvas);
    void paintDecorations(SkCanvas* canvas);

    // TODO: This is a poor mimicing of Minikin decorations; needs some work
    SkScalar computeDecorationThickness(SkTextStyle textStyle);
    SkScalar computeDecorationPosition(SkScalar thickness);
    void computeDecorationPaint(SkPaint& paint, SkPath& path);

    SkFont fFont;
    SkShaper::RunHandler::RunInfo fInfo;
    SkSTArray<128, SkGlyphID, true> fGlyphs;
    SkSTArray<128, SkPoint, true> fPositions;
    SkSTArray<128, uint32_t, true> fClusters;

    SkSpan<const char> fText;
    SkTextStyle fStyle; // TODO: Either we keep the style here or recompute it at painting
    sk_sp<SkTextBlob> fBlob;
    SkRect fRect;
    SkScalar fShift;
};