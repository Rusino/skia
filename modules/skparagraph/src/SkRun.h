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

class SkFontSizes {
  SkScalar fAscent;
  SkScalar fDescent;
  SkScalar fLeading;

 public:
  SkFontSizes() {
    clean();
  }
  SkFontSizes(SkScalar a, SkScalar d, SkScalar l) {
    fAscent = a;
    fDescent = a;
    fLeading = l;
  }
  void add(SkScalar a, SkScalar d, SkScalar l) {
    fAscent = SkTMin(fAscent, a);
    fDescent = SkTMax(fDescent, d);
    fLeading = SkTMax(fLeading, l);
  }
  void add (SkFontSizes other) {
    add(other.fAscent, other.fDescent, other.fLeading);
  }
  void clean() {
    fAscent = 0;
    fDescent = 0;
    fLeading = 0;
  }
  SkScalar diff(SkFontSizes maxSizes) const {
    return ascent() - maxSizes.ascent() - leading() / 2;
  }
  SkScalar height() const {
    return fDescent - fAscent + fLeading;
  }
  SkScalar leading() const { return fLeading; }
  SkScalar ascent() const { return fAscent; }
  SkScalar descent() const { return fDescent; }
};

struct SkCluster {

  enum BreakType {
    None,
    WordBoundary,             // calculated for all clusters (UBRK_WORD)
    WordBreakWithoutHyphen,   // calculated only for hyphenated words
    WordBreakWithHyphen,
    SoftLineBreak,            // calculated for all clusters (UBRK_LINE)
    HardLineBreak,            // calculated for all clusters (UBRK_LINE)
  };

  SkCluster() : fRun(nullptr), fWhiteSpaces(false), fIgnore(false), fBreakType(None) { }
  SkCluster(SkRun* run, size_t start, size_t end, SkSpan<const char> text, SkScalar width, SkScalar height)
    : fText(text), fRun(run)
    , fStart(start), fEnd(end)
    , fWidth(width), fHeight(height)
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
  inline void setBreakType(BreakType type) { fBreakType = type; }
  inline BreakType getBreakType() const { return fBreakType; }
  inline void setIsWhiteSpaces(bool ws) { fWhiteSpaces = ws; }
  inline bool isWhitespaces() const { return fWhiteSpaces; }
  inline bool isIgnored() const { return fIgnore; }
  void ignore() { fIgnore = true; }
  bool canBreakLineAfter() const { return fBreakType == SoftLineBreak ||
                                          fBreakType == HardLineBreak; }
  bool isHardBreak() const { return fBreakType == HardLineBreak; }

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

  SkSpan<const char> fText;

  SkRun* fRun;
  size_t fStart;
  size_t fEnd;

  SkScalar fWidth;
  SkScalar fHeight;
  SkScalar fShift;

  bool fWhiteSpaces;
  bool fIgnore;

  BreakType fBreakType;
};

class SkRun {
 public:

  SkRun() : fFont() { }
  SkRun(SkSpan<const char> text, const SkShaper::RunHandler::RunInfo& info, size_t index, SkScalar shiftX);
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
    return SkVector::Make(fAdvance.fX,
                          fFontMetrics.fDescent + fFontMetrics.fLeading - fFontMetrics.fAscent);
  }
  inline SkVector offset() const { return fOffset; }
  inline SkScalar ascent() const { return fFontMetrics.fAscent; }
  inline SkScalar descent() const { return fFontMetrics.fDescent; }
  inline SkScalar leading() const { return fFontMetrics.fLeading; }
  inline const SkFont& font() const { return fFont ; };
  bool leftToRight() const { return fBidiLevel % 2 == 0; }
  size_t index() const { return fIndex; }

  inline SkSpan<const char> text() const { return fText; }
  inline size_t clusterIndex(size_t pos) const { return fClusterIndexes[pos]; }
  inline SkPoint position(size_t pos) const {
    if (pos < size()) {
      return fPositions[pos];
    }
    return SkVector::Make(
        fAdvance.fX - (fPositions[size() - 1].fX - fPositions[0].fX),
        fAdvance.fY);
  }
  inline SkSpan<SkCluster> clusters() const { return fClusters; }
  inline void setClusters(SkSpan<SkCluster> clusters) { fClusters = clusters; }
  SkRect clip() {
    return SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY);
  }

  SkScalar calculateHeight() const;
  SkScalar calculateWidth(size_t start, size_t end) const;

  SkFontSizes sizes() const { return { fFontMetrics.fAscent, fFontMetrics.fDescent, fFontMetrics.fLeading }; }

  void copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size, SkVector offset) const;

  void iterateThroughClusters(std::function<void(
      size_t glyphStart, size_t glyphEnd, size_t charStart, size_t charEnd, SkVector size)> apply);

 private:

  friend class SkParagraphImpl;
  friend class SkLine;

  SkFont fFont;
  SkFontMetrics fFontMetrics;
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
  SkSTArray<128, uint32_t, true> fClusterIndexes;
};
