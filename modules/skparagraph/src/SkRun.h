/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <unicode/brkiter.h>
#include "uchar.h"
#include "SkSpan.h"
#include "SkShaper.h"
#include "SkFontMetrics.h"

class SkRun;
class SkCluster {

 public:
  enum BreakType {
    None,
    WordBoundary,             // calculated for all clusters (UBRK_WORD)
    WordBreakWithoutHyphen,   // calculated only for hyphenated words
    WordBreakWithHyphen,
    SoftLineBreak,            // calculated for all clusters (UBRK_LINE)
    HardLineBreak,            // calculated for all clusters (UBRK_LINE)
  };

  SkCluster()
  : fText(nullptr, 0), fRun(nullptr)
  , fStart(0), fEnd()
  , fWidth(), fSpacing(0), fHeight()
  , fWhiteSpaces(false)
  , fBreakType(None) { }

  SkCluster(SkRun* run, size_t start, size_t end, SkSpan<const char> text, SkScalar width, SkScalar height)
    : fText(text), fRun(run)
    , fStart(start), fEnd(end)
    , fWidth(width), fSpacing(0), fHeight(height)
    , fWhiteSpaces(false), fBreakType(None) { }

  ~SkCluster() = default;

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

    return fWidth * ratio;
  }
  void space(SkScalar shift, SkScalar space) {
    fSpacing += space;
    fWidth += shift;
  }
  inline void setBreakType(BreakType type) { fBreakType = type; }
  inline void setIsWhiteSpaces(bool ws) { fWhiteSpaces = ws; }
  inline bool isWhitespaces() const { return fWhiteSpaces; }
  bool canBreakLineAfter() const { return fBreakType == SoftLineBreak ||
                                          fBreakType == HardLineBreak; }
  bool isHardBreak() const { return fBreakType == HardLineBreak; }
  bool isSoftBreak() const { return fBreakType == SoftLineBreak; }
  SkRun* run() const { return fRun; }
  size_t startPos() const { return fStart; }
  size_t endPos() const { return fEnd; }
  SkScalar width() const { return fWidth; }
  SkScalar trimmedWidth() const { return fWidth - fSpacing; }
  SkScalar lastSpacing() const { return fSpacing; }
  SkScalar height() const { return fHeight; }
  SkSpan<const char> text() const { return fText; }
  BreakType breakType() const { return fBreakType; }

  void setIsWhiteSpaces() {
    auto pos = fText.end();
    while (--pos >= fText.begin()) {
      auto ch = *pos;
      if (!u_isspace(ch) &&
          u_charType(ch) != U_CONTROL_CHAR &&
          u_charType(ch) != U_NON_SPACING_MARK) {
        return;
      }
    }
    fWhiteSpaces = true;
  }

  bool contains(const char* ch) {
    return ch >= fText.begin() && ch < fText.end();
  }

  bool belongs(SkSpan<const char> text) {
    return fText.begin() >= text.begin() && fText.end() <= text.end();
  }

  bool startsIn(SkSpan<const char> text) {
    return fText.begin() >= text.begin() && fText.begin() < text.end();
  }

 private:
  SkSpan<const char> fText;

  SkRun* fRun;
  size_t fStart;
  size_t fEnd;
  SkScalar fWidth;
  SkScalar fSpacing;
  SkScalar fHeight;
  bool fWhiteSpaces;
  BreakType fBreakType;
};

class SkRun {
 public:

  SkRun() : fFont() { }
  SkRun(SkSpan<const char> text,
        const SkShaper::RunHandler::RunInfo& info,
        SkScalar lineHeight,
        size_t index,
        SkScalar shiftX);
  ~SkRun() { }

  SkShaper::RunHandler::Buffer newRunBuffer();

  inline size_t size() const { return fGlyphs.size(); }
  void setWidth(SkScalar width) { fAdvance.fX = width; }
  void setHeight(SkScalar height) { fAdvance.fY = height; }
  void shift(SkScalar shiftX, SkScalar shiftY) {
    fOffset.fX += shiftX;
    fOffset.fY += shiftY;
  }
  SkVector advance() const {
    return SkVector::Make(fAdvance.fX, fFontMetrics.fDescent - fFontMetrics.fAscent);
  }
  inline SkVector offset() const { return fOffset; }
  inline SkScalar ascent() const { return fFontMetrics.fAscent; }
  inline SkScalar descent() const { return fFontMetrics.fDescent; }
  inline SkScalar leading() const { return fFontMetrics.fLeading; }
  inline const SkFont& font() const { return fFont ; };
  bool leftToRight() const { return fBidiLevel % 2 == 0; }
  size_t index() const { return fIndex; }
  SkScalar lineHeight() const { return fLineHeight; }

  inline SkSpan<const char> text() const { return fText; }
  inline size_t clusterIndex(size_t pos) const { return fClusterIndexes[pos]; }
  SkScalar positionX(size_t pos) const {
    return fPositions[pos].fX + fOffsets[pos];
  }
  inline SkSpan<SkCluster> clusters() const { return fClusters; }
  inline void setClusters(SkSpan<SkCluster> clusters) { fClusters = clusters; }
  SkRect clip() const {
    return SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY);
  }

  SkScalar addSpacesAtTheEnd(SkScalar space, SkCluster* cluster) {
    if (cluster->endPos() == cluster->startPos()) {
      return 0;
    }

    fOffsets[cluster->endPos() - 1] += space;
    // Increment the run width
    fSpaced = true;
    fAdvance.fX += space;
    // Increment the cluster width
    cluster->space(space, space);

    return space;
  }

  SkScalar addSpacesEvenly(SkScalar space, SkCluster* cluster) {
    // Offset all the glyphs in the cluster
    SkScalar shift = 0;
    for (size_t i = cluster->startPos(); i < cluster->endPos(); ++i) {
      fOffsets[i] += shift;
      shift += space;
    }
    // Increment the run width
    fSpaced = true;
    fAdvance.fX += shift;
    // Increment the cluster width
    cluster->space(shift, space);

    return shift;
  }


  void shift(SkCluster* cluster, SkScalar offset) {
    for (size_t i = cluster->startPos(); i < cluster->endPos(); ++i) {
      fOffsets[i] += offset;
    }
  }

  SkScalar calculateHeight() const {
    return fFontMetrics.fDescent - fFontMetrics.fAscent;
  }

  SkScalar calculateWidth(size_t start, size_t end) const;

  void copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size, SkVector offset) const;

  void iterateThroughClustersInTextOrder(std::function<void(
      size_t glyphStart,
      size_t glyphEnd,
      size_t charStart,
      size_t charEnd,
      SkScalar width,
      SkScalar height)> apply);

  std::tuple<bool, SkCluster*, SkCluster*> findClusters(SkSpan<const char> text);

 private:

  friend class SkParagraphImpl;
  friend class SkLine;
  friend class SkLineMetrics;

  SkFont fFont;
  SkFontMetrics fFontMetrics;
  SkScalar fLineHeight;
  size_t fIndex;
  uint8_t fBidiLevel;
  SkVector fAdvance;
  size_t glyphCount;
  SkSpan<const char> fText;
  SkSpan<SkCluster> fClusters;
  SkVector fOffset;
  SkShaper::RunHandler::Range fUtf8Range;
  SkSTArray<128, SkGlyphID, false> fGlyphs;
  SkSTArray<128, SkPoint, true> fPositions;
  SkSTArray<128, SkScalar, true> fOffsets;
  SkSTArray<128, uint32_t, true> fClusterIndexes;
  bool fJustified;
  bool fSpaced;
};

class SkLineMetrics {
 public:
  SkLineMetrics() { clean(); }

  SkLineMetrics(SkScalar a, SkScalar d, SkScalar l) {
    fAscent = a;
    fDescent = d;
    fLeading = l;
  }

  void add (SkRun* run) {
    fAscent = SkTMin(fAscent, run->ascent() * run->lineHeight());
    fDescent = SkTMax(fDescent, run->descent() * run->lineHeight());
    fLeading = SkTMax(fLeading, run->leading() * run->lineHeight());
  }

  void add (SkLineMetrics other) {
    fAscent = SkTMin(fAscent, other.fAscent);
    fDescent = SkTMax(fDescent, other.fDescent);
    fLeading = SkTMax(fLeading, other.fLeading);
  }
  void clean() {
    fAscent = 0;
    fDescent = 0;
    fLeading = 0;
  }

  SkScalar delta() const { return height() - ideographicBaseline(); }

  void updateLineMetrics(SkLineMetrics& metrics, bool forceHeight) {
    if (forceHeight) {
      metrics.fAscent = fAscent;
      metrics.fDescent = fDescent;
      metrics.fLeading = fLeading;
    } else {
      metrics.fAscent = SkTMin(metrics.fAscent, fAscent);
      metrics.fDescent = SkTMax(metrics.fDescent, fDescent);
      metrics.fLeading = SkTMax(metrics.fLeading, fLeading);
    }
  }

  SkScalar runTop(SkRun* run) const {
    return fLeading / 2 - fAscent + run->ascent() + delta();
  }
  inline SkScalar height() const { return SkScalarRoundToInt(fDescent - fAscent + fLeading); }
  inline SkScalar alphabeticBaseline() const { return fLeading / 2 - fAscent;  }
  inline SkScalar ideographicBaseline() const { return fDescent - fAscent + fLeading; }
  inline SkScalar baseline() const { return fLeading / 2 - fAscent; }
  inline SkScalar ascent() const { return fAscent; }
  inline SkScalar descent() const { return fDescent; }
  inline SkScalar leading() const { return fLeading; }

 private:

  SkScalar fAscent;
  SkScalar fDescent;
  SkScalar fLeading;
};