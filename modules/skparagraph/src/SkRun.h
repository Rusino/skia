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
#include "SkTHash.h"
#include "SkArraySpan.h"

class SkRun;
struct SkGlyphsPos {

  explicit SkGlyphsPos(SkRun* run) : fRun(run), fPos(0), fShift(0) {}
  SkGlyphsPos() : fRun(nullptr) {}
  SkGlyphsPos(SkRun* run, size_t pos, SkScalar shift)
      : fRun(run), fPos(pos), fShift(shift) {}
  SkRun* fRun;
  size_t fPos;
  SkScalar fShift;
};

struct SkCluster {

  SkCluster() : fRunIndex(0) { }

  SkScalar sizeToChar(const char* ch) const {

    if (ch < fText.begin() || ch >= fText.end()) {
      return 0;
    }
    auto shift = ch - fText.begin();
    auto ratio = shift * 1.0 / fText.size();

    return fWidth * ratio;
  }

  SkScalar sizeFromChar(const char* ch) const {

    if (ch < fText.begin() || ch >= fText.end()) {
      return 0;
    }
    auto shift = fText.end() - ch - 1;
    auto ratio = shift * 1.0 / fText.size();

    return fWidth * (1 - ratio);
  }

  SkSpan<const char> fText;

  size_t fRunIndex;
  size_t fStart;
  size_t fEnd;

  SkScalar fWidth;
  SkScalar fHeight;
};

// The smallest part of the text that is painted separately
class SkRun {
 public:

  SkRun() {}
  SkRun(
      size_t index,
      const SkFont& font,
      const SkShaper::RunHandler::RunInfo& info,
      int glyphCount,
      SkSpan<const char> text);

  SkShaper::RunHandler::Buffer newRunBuffer();

  inline size_t size() const { return fGlyphs.size(); }
  void setWidth(SkScalar width) { fInfo.fAdvance.fX = width; }
  SkVector advance() const {
    return SkVector::Make(fInfo.fAdvance.fX,
                          fInfo.fDescent + fInfo.fLeading - fInfo.fAscent);
  }
  inline SkVector offset() const { return fInfo.fOffset; }
  inline SkScalar ascent() const { return fInfo.fAscent; }
  inline SkScalar descent() const { return fInfo.fDescent; }
  inline SkScalar leading() const { return fInfo.fLeading; }

  inline SkSpan<const char> text() const { return fText; }
  inline size_t cluster(size_t pos) const { return fClusters[pos]; }

  static SkGlyphsPos findPosition(SkSpan<SkRun> runs, const char* character);
  static void iterateThrough(SkSpan<SkRun> runs, std::function<bool(SkCluster)> apply);

  void iterateThrough(std::function<bool(SkCluster)> apply);

  bool findCluster(const char* character, SkCluster& cluster);
  SkScalar calculateWidth(size_t start, size_t end);
  SkScalar calculateHeight();

 private:

  friend class SkSection;
  friend class SkLine;

  size_t fIndex;
  SkFont fFont;
  SkShaper::RunHandler::RunInfo fInfo;
  SkSTArray<128, SkGlyphID, true> fGlyphs;
  SkSTArray<128, SkPoint, true> fPositions;
  SkSTArray<128, uint32_t, true> fClusters;

  SkSpan<const char> fText;
};