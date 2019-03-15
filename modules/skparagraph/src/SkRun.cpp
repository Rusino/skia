/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkRun.h"
#include "SkSpan.h"

SkRun::SkRun(
    size_t index,
    const SkFont& font,
    const SkShaper::RunHandler::RunInfo& info,
    int glyphCount,
    SkSpan<const char> text)
    : fIndex(index)
    , fFont(font)
    , fInfo(info)
    , fGlyphs(glyphCount)
    , fPositions(glyphCount)
    , fClusters(glyphCount)
    , fText(text)
     {

    fInfo.fOffset.fY = 0;
    fGlyphs.push_back_n(glyphCount);
    fPositions.push_back_n(glyphCount);
    fClusters.push_back_n(glyphCount);
}

SkShaper::RunHandler::Buffer SkRun::newRunBuffer() {

    return {
        fGlyphs.data(),
        fPositions.data(),
        fClusters.data()
    };
}

SkScalar SkRun::calculateHeight() {
  return fInfo.fDescent - fInfo.fAscent + fInfo.fLeading;
}

