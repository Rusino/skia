/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkRun.h"
#include "SkSpan.h"
#include "SkTHash.h"
#include "SkLine.h"

class SkTextWrapper {
  class Position {
   public:
    Position(const SkCluster* start) {
      clean(start);
    }
    inline SkScalar width() const { return fWidth + fWhitespaces; }
    inline SkScalar trimmedWidth() const { return fWidth; }
    inline SkScalar height() const { return fHeight; }
    inline const SkCluster* trimmed() { return fTrimmedEnd; }
    inline const SkCluster* end() { return fEnd; }
    inline SkVector trimmedAdvance() { return SkVector::Make(fWidth, fHeight); }
    void clean(const SkCluster* start) {
      fEnd = start;
      fTrimmedEnd = start;
      fWidth = 0;
      fHeight = 0;
      fWhitespaces = 0;
    }
    void add(Position& other) {
      this->fWidth += this->fWhitespaces + other.fWidth;
      this->fHeight = SkTMax(this->fHeight, other.fHeight);
      this->fTrimmedEnd = other.fTrimmedEnd;
      this->fEnd = other.fEnd;
      this->fWhitespaces = other.fWhitespaces;
      other.clean(other.fEnd);
    }
    void add(const SkCluster& cluster) {
      if (cluster.isWhitespaces()) {
        fWhitespaces += cluster.fWidth;
      } else {
        fTrimmedEnd = &cluster;
        fWidth += cluster.fWidth + fWhitespaces;
        fWhitespaces = 0;
      }
      fEnd = &cluster;
      fHeight = SkTMax(fHeight, cluster.fHeight);
    }
    void extend(SkScalar w) { fWidth += w; }
    SkSpan<const char> trimmedText(const SkCluster* start) {
      return SkSpan<const char>(start->fText.begin(), fTrimmedEnd->fText.end() - start->fText.begin());
    }
   private:
    SkScalar fWidth;
    SkScalar fHeight;
    SkScalar fWhitespaces;
    const SkCluster* fEnd;
    const SkCluster* fTrimmedEnd;
  };
 public:

  SkTextWrapper() : fClosestBreak(nullptr), fAfterBreak(nullptr) { }
  void formatText(SkSpan<SkCluster> clusters,
                  SkScalar maxWidth,
                  size_t maxLines,
                  const std::string& ellipsis);
  SkSpan<SkLine> getLines() { return SkSpan<SkLine>(fLines.begin(), fLines.size()); }
  SkSpan<const SkLine> getConstLines() const { return SkSpan<const SkLine>(fLines.begin(), fLines.size()); }
  SkLine* getLastLine() { return &fLines.back(); }

  inline SkScalar height() const { return fHeight; }
  inline SkScalar width() const { return fWidth; }

  void reset() { fLines.reset() ;}

 private:

  bool endOfText() const { return fLineStart == fClusters.end(); }
  bool reachedLinesLimit(int32_t delta) const {
    return fMaxLines != std::numeric_limits<size_t>::max() && fLines.size() >= fMaxLines + delta;
  }
  SkRun* createEllipsis(Position& pos);
  bool addLine(Position& pos);
  SkRun* shapeEllipsis(SkRun* run);
  SkRun* getEllipsis(SkRun* run);

  SkSpan<SkCluster> fClusters;
  SkTArray<SkLine, true> fLines;
  SkScalar fMaxWidth;
  size_t fMaxLines;
  std::string fEllipsis;
  const SkCluster* fLineStart;
  Position fClosestBreak;
  Position fAfterBreak;
  SkVector fCurrentLineOffset;
  SkVector fCurrentLineAdvance;
  SkScalar fWidth;
  SkScalar fHeight;

  // TODO: make a static cache
  SkTHashMap<SkFont, SkRun> fEllipsisCache; // All found so far shapes of ellipsis
};