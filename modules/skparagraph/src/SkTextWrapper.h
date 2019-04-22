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

class SkParagraphImpl;
class SkTextWrapper {

  class Position {
    std::string toString(SkSpan<const char> text) {
      icu::UnicodeString
          utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
      std::string str;
      utf16.toUTF8String(str);
      return str;
    }
   public:
    Position(const SkCluster* start) {
      if (start != nullptr) {
        clean(start);
      } else {
        fEnd = nullptr;
        fTrimmedEnd = nullptr;
      }
    }
    inline SkScalar width() const { return fWidth + fWhitespaces.fX; }
    inline SkScalar trimmedWidth() const { return fWidth; }
    inline SkScalar height() const {
      return fWidth == 0 ? fWhitespaces.fY : fSizes.height();
    }
    inline const SkCluster* trimmed() const { return fTrimmedEnd; }
    inline const SkCluster* end() const { return fEnd; }
    inline SkFontSizes sizes() const { return fSizes; }

    void clean(const SkCluster* start) {
      fEnd = start;
      fTrimmedEnd = start;
      fWidth = 0;
      fWhitespaces = SkVector::Make(0, 0);
      fSizes.clean();
    }

    SkScalar add(Position& other) {

      auto result = other.fWidth;
      this->fWidth += this->fWhitespaces.fX + other.fWidth;
      this->fTrimmedEnd = other.fTrimmedEnd;
      this->fEnd = other.fEnd;
      this->fWhitespaces = other.fWhitespaces;
      this->fSizes.add(other.fSizes);
      other.clean(other.fEnd);

      return result;
    }

    void add(const SkCluster& cluster) {
      if (cluster.isWhitespaces()) {
        fWhitespaces.fX += cluster.fWidth;
        fWhitespaces.fY = SkTMax(fWhitespaces.fY, cluster.fRun->calculateHeight());
      } else {
        fTrimmedEnd = &cluster;
        fWidth += cluster.fWidth + fWhitespaces.fX;
        fWhitespaces = SkVector::Make(0, 0);
        fSizes.add(cluster.fRun->ascent(), cluster.fRun->descent(), cluster.fRun->leading());
      }
      fEnd = &cluster;
    }

    void extend(SkScalar w) { fWidth += w; }
    void trim(const SkCluster* end) { fTrimmedEnd = end; }

    SkSpan<const char> trimmedText(const SkCluster* start) {
      size_t size = fTrimmedEnd->fText.end() > start->fText.begin()
                    ? fTrimmedEnd->fText.end() - start->fText.begin() : 0;
      return SkSpan<const char>(start->fText.begin(), size);
    }

   private:
    SkScalar fWidth;
    SkFontSizes fSizes;
    SkVector fWhitespaces;
    const SkCluster* fEnd;
    const SkCluster* fTrimmedEnd;
  };

 public:

  SkTextWrapper(SkParagraphImpl* parent)
  : fParent(parent), fClosestBreak(nullptr), fAfterBreak(nullptr) { reset(); }
  void formatText(SkSpan<SkCluster> clusters,
                  SkScalar maxWidth,
                  size_t maxLines,
                  const std::string& ellipsis);

  inline SkScalar height() const { return fHeight; }
  inline SkScalar width() const { return fWidth; }
  inline SkScalar intrinsicWidth() const { return fMinIntrinsicWidth; }


  void reset() {
    fLineStart = nullptr;
    fClosestBreak = nullptr;
    fAfterBreak = nullptr;
    fMinIntrinsicWidth = 0;
    fCurrentLineOffset = SkVector::Make(0, 0);
    fWidth = 0;
    fHeight = 0;
  }

 private:

  bool endOfText() const { return fLineStart == fClusters.end(); }

  std::unique_ptr<SkRun> createEllipsis(Position& pos, bool ltr);
  bool addLine(Position& pos);
  SkRun* shapeEllipsis(SkRun* run);

  SkParagraphImpl* fParent;
  SkSpan<SkCluster> fClusters;
  std::string fEllipsis;
  const SkCluster* fLineStart;
  Position fClosestBreak;
  Position fAfterBreak;
  SkVector fCurrentLineOffset;
  SkScalar fMaxWidth;
  SkScalar fWidth;
  SkScalar fHeight;
  SkScalar fMinIntrinsicWidth;

  // TODO: make a static cache?
  SkTHashMap<SkFont, SkRun> fEllipsisCache; // All found so far shapes of ellipsis
};
