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
#include <unicode/brkiter.h>

class SkRun;
struct SkCluster {

  enum BreakType {
    None,
    WordBreak,
    SoftLineBreak,
    HardLineBreak
  };

  SkCluster() : fRun(nullptr), fWhiteSpaces(false), fIgnore(false), fBreakType(None) { }

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
  inline void setBreakType(BreakType type) { fBreakType = type; }
  inline BreakType getBreakType() const { return fBreakType; }
  inline void setIsWhiteSpaces(bool ws) { fWhiteSpaces = ws; }
  inline bool isWhitespaces() const { return fWhiteSpaces; }
  inline bool isIgnored() const { return fIgnore; }
  void ignore() { fIgnore = true; }
  bool canBreakLineAfter() { return fBreakType == SoftLineBreak ||
                                    fBreakType == HardLineBreak; }

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

  bool fWhiteSpaces;
  bool fIgnore;

  BreakType fBreakType;
};

class SkRun {
 public:

  SkRun() {}
  SkRun(size_t index,
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
  inline SkPoint position(size_t pos) const {
    if (pos < size()) {
      return fPositions[pos];
    }
    return SkVector::Make(
        fInfo.fAdvance.fX - (fPositions[size() - 1].fX - fPositions[0].fX),
        fInfo.fAdvance.fY);
  }

  SkScalar calculateHeight();
  SkScalar calculateWidth(size_t start, size_t end);
  void setText(size_t start, size_t end) {
    fText = SkSpan<const char>(fText.begin() + start, end - start);
  }

  void copyTo(SkTextBlobBuilder& builder, size_t pos, size_t size) const;

 private:

  size_t fIndex;
  SkFont fFont;
  SkShaper::RunHandler::RunInfo fInfo;
  SkSTArray<128, SkGlyphID, true> fGlyphs;
  SkSTArray<128, SkPoint, true> fPositions;
  SkSTArray<128, uint32_t, true> fClusters;

  SkSpan<const char> fText;
};