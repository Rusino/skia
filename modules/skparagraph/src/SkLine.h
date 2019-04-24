/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <SkTHash.h>
#include "SkDartTypes.h"
#include "SkSpan.h"
#include "SkTArray.h"
#include "SkCanvas.h"
#include "SkTextStyle.h"
#include "SkRun.h"

class SkBlock {
 public:

  SkBlock() : fText(), fTextStyle() {}
  SkBlock(SkSpan<const char> text, const SkTextStyle& style)
      : fText(text), fTextStyle(style) {
  }

  inline SkSpan<const char> text() const { return fText; }
  inline SkTextStyle style() const { return fTextStyle; }

 protected:
  SkSpan<const char> fText;
  SkTextStyle fTextStyle;
};

class SkWord {

 public:

  SkWord() { }

  SkWord(SkSpan<const char> text)
      : fText(text)
      , fShift(0)
      , fAdvance(SkVector::Make(0, 0)) {
    setIsWhiteSpaces();
  }

  inline SkSpan<const char> text() const { return fText; }
  //inline SkVector advance() const { return fAdvance; }
  //inline SkScalar offset() const { return fShift; }
  inline void shift(SkScalar shift) { fShift += shift; }
  inline void expand(SkScalar step) { fAdvance.fX += step; }
  inline bool empty() const { return fText.empty(); }
  inline bool isWhiteSpace() const { return fWhiteSpaces; }

 private:

  friend class SkLine;

  void setIsWhiteSpaces() {
    fWhiteSpaces = false;
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
  SkScalar fShift;    // For justification
  SkVector fAdvance;  // Size
  bool fWhiteSpaces;
};

class SkLine {

 public:

  SkLine() { }

  SkLine(const SkLine&);

  ~SkLine() {
    fWords.reset();
  }

  SkLine(SkVector offset
        , SkVector advance
        , SkSpan<SkCluster> clusters
        , SkSpan<const char> text
        , SkRunMetrics sizes
        , bool ltr)
      : fText(text)
      , fClusters(clusters)
      , fLogical()
      , fShift(0)
      , fAdvance(advance)
      //, fWidth(advance.fX)
      , fOffset(offset)
      , fEllipsis(nullptr)
      , fSizes(sizes)
      , fLeftToRight(ltr) { }

  inline SkSpan<const char> text() const { return fText; }
  inline SkSpan<SkCluster> clusters() const { return fClusters; }
  inline SkVector offset() const { return fOffset + SkVector::Make(fShift, 0); }
  inline SkRun* ellipsis() const { return fEllipsis.get(); }
  inline SkRunMetrics sizes() const { return fSizes; }
  inline bool empty() const { return fText.empty(); }
  void breakLineByWords(UBreakIteratorType type, std::function<void(SkWord& word)> apply);
  void reorderVisualRuns();
  SkScalar height() const { return fAdvance.fX; }
  SkScalar width() const { return fAdvance.fX + (fEllipsis != nullptr ? fEllipsis->fAdvance.fX : 0); }
  void setWidth(SkScalar width) { fAdvance.fX = width - (fEllipsis != nullptr ? fEllipsis->fAdvance.fX : 0); }
  SkScalar shift() const { return fShift; }
  void shiftTo(SkScalar shift) { fShift = shift; }

  SkRect measureTextInsideOneRun(SkSpan<const char> text,
                                 SkRun* run,
                                 size_t& pos,
                                 size_t& size) const;
  SkVector measureWordAcrossAllRuns(SkSpan<const char> text) const;
  void justify(SkScalar maxWidth);
  void setEllipsis(std::unique_ptr<SkRun> ellipsis) { fEllipsis = std::move(ellipsis); }

  void iterateThroughStyles(
      SkStyleType styleType,
      SkSpan<SkBlock> blocks,
      std::function<SkScalar(
          SkSpan<const char> text,
          const SkTextStyle& style,
          SkScalar offsetX)> apply) const;

  SkScalar iterateThroughRuns(
      SkSpan<const char> text,
      SkScalar offsetX,
      std::function<void(SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift)> apply) const;

  void iterateThroughClustersInGlyphsOrder(bool reverse,
                                           std::function<bool(const SkCluster* cluster)> apply) const;

  bool paint(SkCanvas* canvas, SkSpan<SkBlock> blocks);
  SkScalar paintText(
      SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkScalar offsetX) const;
  SkScalar paintBackground(
      SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkScalar offsetX) const;
  SkScalar paintShadow(
      SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkScalar offsetX) const;
  SkScalar paintDecorations(
      SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkScalar offsetX) const;

  void computeDecorationPaint(SkPaint& paint, SkRect clip, const SkTextStyle& style, SkPath& path) const;

  void createEllipsis(SkScalar maxWidth, const std::string& ellipsis, bool ltr);

 private:

  SkRun* shapeEllipsis(const std::string& ellipsis, SkRun* run);

  bool contains(const SkCluster* cluster) const {
    return cluster->text().begin() >= fText.begin() && cluster->text().end() <= fText.end();
  }

  SkSpan<const char> fText;
  SkSpan<SkCluster> fClusters;
  SkTArray<SkRun*, true> fLogical;
  SkTArray<SkWord, true> fWords; // Text broken into words by ICU word breaker
  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;  // Text on the line size
  //SkScalar fWidth;
  SkVector fOffset;   // Text position on the screen
  std::unique_ptr<SkRun> fEllipsis;   // In case the line ends with the ellipsis
  SkRunMetrics fSizes;
  bool fLeftToRight;

  static SkTHashMap<SkFont, SkRun> fEllipsisCache; // All found so far shapes of ellipsis
};

