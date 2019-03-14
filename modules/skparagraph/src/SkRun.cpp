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

bool SkRun::findCluster(const char* ch, SkCluster& data) {

  bool found = false;
  iterateThrough([&data, &found, ch](SkCluster cluster) -> bool {
    if (cluster.fText.begin() <= ch && cluster.fText.end() > ch) {
      data = cluster;
      found = true;
    }
    return found;
  });
  return found;
}

SkScalar SkRun::calculateWidth(size_t start, size_t end) {

  return (end == size()
          ? fInfo.fAdvance.fX + fPositions[0].fX
          : fPositions[end].fX) - fPositions[start].fX;
}

SkScalar SkRun::calculateHeight() {
  return fInfo.fDescent - fInfo.fAscent + fInfo.fLeading;
}

SkGlyphsPos SkRun::findPosition(SkSpan<SkRun> runs, const char* character) {
  // Find the run
  SkRun* run;
  for (run = runs.begin(); run != runs.end(); ++run) {
    if (run->fText.begin() <= character && run->fText.end() > character) {
      break;
    }
  }
  if (run == runs.end()) {
    return SkGlyphsPos();
  }

  // Find the cluster and it's glyphs[gStart:gEnd]
  size_t num = character - run->fText.begin();
  size_t gStart = 0;
  size_t gEnd = run->size();
  for (size_t i = 0; i < run->size(); ++i) {
    auto cl = run->fClusters[i];
    if (cl <= num) {
      gStart = i;
      continue;
    }
    if (i < gEnd) {
      gEnd = i;
      break;
    }
  }

  // Find the char projection of a cluster
  auto cluster = run->fClusters[gStart];
  auto cStart = run->fText.begin() + cluster;
  auto cEnd = gEnd == run->size() ? run->fText.end() : run->fText.begin() + run->fClusters[gEnd];

  auto ratio = 1.0 * (cEnd - character)/(cEnd - cStart);
  auto len = gEnd == run->size() ? run->fInfo.fAdvance.fX : run->fPositions[gEnd].fX - run->fPositions[gStart].fX;

  return SkGlyphsPos(run, cStart - run->fText.begin(), SkDoubleToScalar(ratio * len));
}

void SkRun::iterateThrough(std::function<bool(SkCluster)> apply) {
  size_t prevCluster = 0;
  size_t prevPos = 0;
  for (size_t pos = 0; pos <= size(); ++pos) {
    auto cluster = pos == size() ? fText.size() : fClusters[pos];
    if (cluster == prevCluster) {
      continue;
    }
    SkCluster data;
    data.fText = SkSpan<const char>(fText.begin() + prevCluster, cluster - prevCluster);
    data.fRunIndex = fIndex;
    data.fStart = prevPos;
    data.fEnd = pos;
    data.fWidth = (pos == size()
                   ? fInfo.fAdvance.fX + fPositions[0].fX
                   : fPositions[pos].fX) - fPositions[prevPos].fX;
    data.fHeight = fInfo.fAdvance.fY;
    if (apply(data)) {
      break;
    }
    prevCluster = cluster;
    prevPos = pos;
  }
}

void SkRun::iterateThrough(SkSpan<SkRun> runs, std::function<bool(SkCluster)> apply) {

  for (auto run = runs.begin(); run != runs.end(); ++run) {

    run->iterateThrough(apply);
  }
}
