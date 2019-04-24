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
  fPositions.push_back_n(info.glyphCount + 1);
  fOffsets.push_back_n(info.glyphCount + 1, SkScalar(0));
  fClusterIndexes.push_back_n(info.glyphCount + 1);
  info.fFont.getMetrics(&fFontMetrics);
  fJustified = false;
  // To make edge cases easier:
  fPositions[info.glyphCount] = fOffset + fAdvance;
  fClusterIndexes[info.glyphCount] = info.utf8Range.end();
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

  SkASSERT(start <= end);
  SkScalar offset = 0;
  if (fJustified && end > start) {
    offset = fOffsets[end - 1] - fOffsets[start];
  }
 return fPositions[end].fX - fPositions[start].fX + offset;

}

void SkRun::copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size, SkVector offset) const {

  SkASSERT(pos + size <= this->size());
  const auto& blobBuffer = builder.allocRunPos(fFont, SkToInt(size));
  sk_careful_memcpy(blobBuffer.glyphs,
                    fGlyphs.data() + pos,
                    size * sizeof(SkGlyphID));

  if (fJustified || offset.fX != 0 || offset.fY != 0) {
    for (size_t i = 0; i < size; ++i) {
      auto point = fPositions[i + pos];
      if (fJustified) {
        point.fX += fOffsets[i + pos];
      }
      blobBuffer.points()[i] = point + offset;
    }
  } else {
    // Good for the first line
    sk_careful_memcpy(blobBuffer.points(),
                      fPositions.data() + pos,
                      size * sizeof(SkPoint));
  }
}

std::tuple<bool, SkCluster*, SkCluster*> SkRun::findClusters(SkSpan<const char> text) {

  auto first = text.begin();
  auto last = text.end() - 1;

  // TODO: Make the search more effective
  SkCluster* start = nullptr;
  SkCluster* end = nullptr;
  for (auto& cluster : fClusters) {
    if (cluster.contains(first)) start = &cluster;
    if (cluster.contains(last)) end = &cluster;
  }
  if (!leftToRight()) {
    std::swap(start, end);
  }

  return std::make_tuple(start != nullptr && end != nullptr, start, end);
}

void SkRun::iterateThroughClustersInTextOrder(std::function<void(
    size_t glyphStart,
    size_t glyphEnd,
    size_t charStart,
    size_t charEnd,
    SkScalar width,
    SkScalar height)> apply) {

  // Can't figure out how to do it with one code for both cases without 100 ifs
  // Can't go through clusters because there are no cluster table yet
  if (leftToRight()) {
    size_t start = 0;
    size_t cluster = this->clusterIndex(start);
    for (size_t glyph = 1; glyph <= this->size(); ++glyph) {

      auto nextCluster = this->clusterIndex(glyph);
      if (nextCluster == cluster) {
        continue;
      }

      apply(start, glyph, cluster, nextCluster, this->calculateWidth(start, glyph), this->calculateHeight());

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

      apply(start, glyph, cluster, nextCluster, this->calculateWidth(start, glyph), this->calculateHeight());

      glyph = start;
      cluster = nextCluster;
    }
  }
}