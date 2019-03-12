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

void SkRun::iterateThrough(SkArraySpan<SkRun> runs, std::function<void(SkCluster)> apply) {

  const char* start = runs.begin()->text().begin();
  const char* end = runs.back()->text().end();
  size_t prevCluster = 0;
  SkRun* prevRun = runs.begin();
  SkScalar width = 0;
  for (auto run = runs.begin(); run != runs.end(); ++run) {
    for (size_t pos = 0; pos != run->size(); ++pos) {
      auto cluster = run->fClusters[pos];
      if (cluster == prevCluster) {
        width += (pos + 1 == run->size()
                  ? run->fInfo.fAdvance.fX + run->fPositions[0].fX
                  : run->fPositions[pos + 1].fX) - run->fPositions[pos].fX;
        continue;
      }
      SkCluster data;
      data.fCluster = SkSpan<const char>(start + prevCluster, cluster - prevCluster);
      data.fRun = prevRun;
      data.fStart = prevCluster;
      data.fEnd = cluster;
      data.fWidth = width;
      data.fHeight = prevRun->fInfo.fAdvance.fY;

      apply(data);

      prevRun = run;
      prevCluster = cluster;
      width = 0;
    }
  }

  SkCluster data;
  data.fCluster = SkSpan<const char>(start + prevCluster, end - start);
  data.fRun = prevRun;
  data.fStart = prevCluster;
  data.fEnd = end - start;
  data.fWidth = width;
  data.fHeight = prevRun->fInfo.fAdvance.fY;
  apply(data);
}