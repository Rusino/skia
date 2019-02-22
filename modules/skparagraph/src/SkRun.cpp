/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkRun.h"

SkRun::SkRun(
    const SkFont& font,
    const SkShaper::RunHandler::RunInfo& info,
    int glyphCount,
    SkSpan<const char> text)
    : fFont(font)
    , fInfo(info)
    , fGlyphs(glyphCount)
    , fPositions(glyphCount)
    , fClusters(glyphCount)
    , fText(text)
     {

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

