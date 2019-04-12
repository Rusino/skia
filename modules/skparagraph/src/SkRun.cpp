/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <SkFontMetrics.h>
#include "SkRun.h"
#include "SkSpan.h"

SkRun::SkRun(const SkShaper::RunHandler::RunInfo& info, SkScalar offsetX, size_t index) {

  fFont = info.fFont;
  fBidiLevel = info.fBidiLevel;
  fAdvance = info.fAdvance;
  glyphCount = info.glyphCount;
  fUtf8Range = info.utf8Range;

  fOffset = SkVector::Make(offsetX, 0);
  fGlyphs.push_back_n(info.glyphCount);
  fPositions.push_back_n(info.glyphCount);
  fClusters.push_back_n(info.glyphCount);
  info.fFont.getMetrics(&fFontMetrics);

  fIndex = index;
}

SkShaper::RunHandler::Buffer SkRun::newRunBuffer() {

    return {
        fGlyphs.data(),
        fPositions.data(),
        nullptr,
        fClusters.data(),
        fOffset
    };
}

SkScalar SkRun::calculateHeight() {
  // The height of the run, not the height of the entire text (fInfo)
  return fFontMetrics.fDescent - fFontMetrics.fAscent + fFontMetrics.fLeading;
}

SkScalar SkRun::calculateWidth(size_t start, size_t end) {
  SkASSERT(start <= end);
  if (end == size()) {
    return fAdvance.fX - fPositions[start].fX + fPositions.front().fX;
  } else {
    return fPositions[end].fX - fPositions[start].fX;
  }
}

void SkRun::copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size, SkVector offset) const {

  const auto& blobBuffer = builder.allocRunPos(fFont, SkToInt(size));
  sk_careful_memcpy(blobBuffer.glyphs,
                    fGlyphs.data() + pos,
                    size * sizeof(SkGlyphID));

  for (size_t i = 0; i < size; ++i) {
    auto point = fPositions[i + pos];
    blobBuffer.points()[i] = point + offset;
  }
  //sk_careful_memcpy(blobBuffer.points(),
  //                  fPositions.data() + pos,
  //                  size * sizeof(SkPoint));
}

// With respect to bidi
void SkRun::iterateThroughClusters(std::function<void(
        size_t glyphStart, size_t glyphEnd, size_t charStart, size_t charEnd, SkVector size)> apply) {

  if (leftToRight()) {
    size_t start = 0;
    size_t cluster = this->cluster(start);
    for (size_t glyph = 1; glyph <= this->size(); ++glyph) {

      auto nextCluster =
          glyph == this->size() ? this->fUtf8Range.end() : this->cluster(glyph);
      if (nextCluster == cluster) {
        continue;
      }

      SkVector size = SkVector::Make(this->calculateWidth(start, glyph), this->calculateHeight());
      apply(start, glyph, cluster, nextCluster, size);

      start = glyph;
      cluster = nextCluster;
    };
  } else {
    size_t start = 0;
    size_t cluster = this->fUtf8Range.end();
    for (size_t glyph = 1; glyph <= this->size(); ++glyph) {

      auto nextCluster = this->cluster(glyph - 1);
      if (nextCluster == cluster) {
        continue;
      }

      SkVector size = SkVector::Make(this->calculateWidth(start, glyph), this->calculateHeight());
      apply(start, glyph, nextCluster, cluster, size);

      start = glyph;
      cluster = nextCluster;
    };
  }

}