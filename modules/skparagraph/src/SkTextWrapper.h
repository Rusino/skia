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

 public:

  class Position {
    std::string toString(SkSpan<const char> text) {
      icu::UnicodeString
          utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
      std::string str;
      utf16.toUTF8String(str);
      return str;
    }
   public:
    explicit Position(const SkCluster* start) {
      clean(start);
    }
    inline SkScalar width() const { return fWidth + fWhitespaces.fX; }
    inline SkScalar trimmedWidth() const { return fWidth; }
    inline SkScalar height() const {
      return fWidth == 0 ? fWhitespaces.fY : fSizes.height();
    }
    inline const SkCluster* trimmed() const { return fTrimmedEnd; }
    inline const SkCluster* end() const { return fEnd; }
    inline SkRunMetrics sizes() const { return fSizes; }

    void clean(const SkCluster* start) {
      fEnd = start;
      fTrimmedEnd = start;
      fWidth = 0;
      fWhitespaces = SkVector::Make(0, 0);
      fSizes.clean();
    }

    SkScalar moveTo(Position& other) {

      auto result = other.fWidth;
      this->fWidth += this->fWhitespaces.fX + other.fWidth;
      this->fTrimmedEnd = other.fTrimmedEnd;
      this->fEnd = other.fEnd;
      this->fWhitespaces = other.fWhitespaces;
      this->fSizes.add(other.fSizes);
      other.clean(other.fEnd);

      return result;
    }

    void moveTo(const SkCluster& cluster) {
      if (cluster.isWhitespaces()) {
        fWhitespaces.fX += cluster.width();
        fWhitespaces.fY = SkTMax(fWhitespaces.fY, cluster.run()->calculateHeight());
      } else {
        fTrimmedEnd = &cluster;
        fWidth += cluster.width() + fWhitespaces.fX;
        fWhitespaces = SkVector::Make(0, 0);
        fSizes.add(cluster.run()->ascent(), cluster.run()->descent(), cluster.run()->leading());
      }
      fEnd = &cluster;
    }

    void extend(SkScalar w) { fWidth += w; }
    void trim(const SkCluster* end) { fTrimmedEnd = end; }

    SkSpan<const char> trimmedText(const SkCluster* start) {
      size_t size = fTrimmedEnd->text().end() > start->text().begin()
                    ? fTrimmedEnd->text().end() - start->text().begin() : 0;
      return SkSpan<const char>(start->text().begin(), size);
    }

   private:
    SkScalar fWidth;
    SkRunMetrics fSizes;
    SkVector fWhitespaces;
    const SkCluster* fEnd;
    const SkCluster* fTrimmedEnd;
  };

  SkTextWrapper(SkParagraphImpl* parent)
  : fParent(parent), fLastBreak(nullptr), fLastPosition(nullptr) { reset(); }

  void formatText(SkSpan<SkCluster> clusters,
                  SkScalar maxWidth,
                  size_t maxLines,
                  const std::string& ellipsis);

  inline SkScalar height() const { return fHeight; }
  inline SkScalar width() const { return fWidth; }
  inline SkScalar intrinsicWidth() const { return fMinIntrinsicWidth; }


  void reset() {
    fLineStart = nullptr;
    fLastBreak.clean(nullptr);
    fLastPosition.clean(nullptr);
    fMinIntrinsicWidth = 0;
    fOffsetY = 0;
    fWidth = 0;
    fHeight = 0;
    fLineNumber = 0;
    fMaxLines = std::numeric_limits<size_t>::max();
  }

 private:

  bool endOfText() const { return fLineStart == fClusters.end(); }
  bool addLineUpToTheLastBreak();
  bool reachedLinesLimit() const {
    return fMaxLines != std::numeric_limits<size_t>::max() && fLineNumber >= fMaxLines;
  }

  SkParagraphImpl* fParent;
  SkSpan<SkCluster> fClusters;
  std::string fEllipsis;
  const SkCluster* fLineStart;
  Position fLastBreak;
  Position fLastPosition;
  SkScalar fOffsetY;
  size_t fLineNumber;
  size_t fMaxLines;
  SkScalar fMaxWidth;
  SkScalar fWidth;
  SkScalar fHeight;
  SkScalar fMinIntrinsicWidth;
};
