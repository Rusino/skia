/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkRun.h"
#include "SkSpan.h"

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

SkScalar SkRun::calculateWidth(size_t start, size_t end) {
  SkASSERT(start <= end);
  if (end == size()) {
    return fInfo.fAdvance.fX - fPositions[start].fX + fPositions[0].fX;
  } else {
   return fPositions[end].fX - fPositions[start].fX;
  }
}

void SkRun::copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size) const {

  const auto& blobBuffer = builder.allocRunPos(fFont, SkToInt(size));
  sk_careful_memcpy(blobBuffer.glyphs,
                    fGlyphs.data() + pos,
                    size * sizeof(SkGlyphID));
  sk_careful_memcpy(blobBuffer.points(),
                    fPositions.data() + pos,
                    size * sizeof(SkPoint));
}