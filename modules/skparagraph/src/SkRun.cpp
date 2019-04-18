/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <SkFontMetrics.h>
#include "SkRun.h"
#include "SkSpan.h"

SkRun::SkRun(SkSpan<const char> text, const SkShaper::RunHandler::RunInfo& info, size_t index, SkScalar offsetX) {

  fFont = info.fFont;
  fBidiLevel = info.fBidiLevel;
  fAdvance = info.fAdvance;
  glyphCount = info.glyphCount;
  fText = SkSpan<const char>(text.begin() + info.utf8Range.begin(), info.utf8Range.size());

  fIndex = index;
  fUtf8Range = info.utf8Range;
  fOffset = SkVector::Make(offsetX, 0);
  fGlyphs.push_back_n(info.glyphCount);
  fPositions.push_back_n(info.glyphCount);
  fClusterIndexes.push_back_n(info.glyphCount);
  info.fFont.getMetrics(&fFontMetrics);
}

SkShaper::RunHandler::Buffer SkRun::newRunBuffer() {

    return {
        fGlyphs.data(),
        fPositions.data(),
        nullptr,
        fClusterIndexes.data(),
        fOffset
    };
}

SkScalar SkRun::calculateHeight() const {
  // The height of the run, not the height of the entire text (fInfo)
  return fFontMetrics.fDescent - fFontMetrics.fAscent + fFontMetrics.fLeading;
}

SkScalar SkRun::calculateWidth(size_t start, size_t end) const {
  if (!leftToRight()) {
    //std::swap(start, end);
  }
  SkASSERT(start <= end);
  if (end == size()) {
    return fAdvance.fX - fPositions[start].fX + fPositions[0].fX;
  } else {
   return fPositions[end].fX - fPositions[start].fX;
  }
}

void SkRun::copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size, SkVector offset) const {

  SkASSERT(pos + size <= this->size());
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

void SkRun::iterateThroughClusters(std::function<void(
    size_t glyphStart, size_t glyphEnd, size_t charStart, size_t charEnd, SkVector size)> apply) {

  if (leftToRight()) {
    size_t start = 0;
    size_t cluster = this->clusterIndex(start);
    for (size_t glyph = 1; glyph <= this->size(); ++glyph) {

      auto nextCluster =
          glyph == this->size() ? this->fUtf8Range.end() : this->clusterIndex(glyph);
      if (nextCluster == cluster) {
        continue;
      }

      SkVector size = SkVector::Make(this->calculateWidth(start, glyph), this->calculateHeight());
      apply(start, glyph, cluster, nextCluster, size);

      start = glyph;
      cluster = nextCluster;
    };
  } else {
    size_t glyph = this->size();
    size_t cluster = this->fUtf8Range.begin();
    for (int32_t start = this->size() - 1; start >= 0; --start) {

      size_t nextCluster =
          start == 0 ? this->fUtf8Range.end() : this->clusterIndex(start - 1);
      if (nextCluster == cluster) {
        continue;
      }

      SkVector size = SkVector::Make(this->calculateWidth(start, glyph), this->calculateHeight());
      apply(start, glyph, cluster, nextCluster, size);

      glyph = start;
      cluster = nextCluster;
    }
  }


}