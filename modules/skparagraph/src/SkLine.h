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

class SkLine {

 public:

  SkLine() { }

  SkLine(const SkLine&);

  ~SkLine() { }

  SkLine(SkVector offset
        , SkVector advance
        , SkSpan<SkCluster> clusters
        , SkSpan<const char> text
        , SkLineMetrics sizes
        , SkLineMetrics strutMetrics)
      : fText(text)
      , fClusters(clusters)
      , fLogical()
      , fShift(0)
      , fAdvance(advance)
      , fOffset(offset)
      , fEllipsis(nullptr)
      , fSizes(sizes)
      , fStrutMetrics(strutMetrics) {
  }

  inline SkSpan<const char> text() const { return fText; }
  inline SkSpan<SkCluster> clusters() const { return fClusters; }
  inline SkVector offset() const { return fOffset + SkVector::Make(fShift, 0); }
  inline SkRun* ellipsis() const { return fEllipsis.get(); }
  inline SkLineMetrics sizes() const { return fSizes; }
  inline bool empty() const { return fText.empty(); }
  void reorderVisualRuns();
  SkScalar height() const { return fAdvance.fY; }
  SkScalar width() const { return fAdvance.fX + (fEllipsis != nullptr ? fEllipsis->fAdvance.fX : 0); }
  void setWidth(SkScalar width) { fAdvance.fX = width - (fEllipsis != nullptr ? fEllipsis->fAdvance.fX : 0); }
  SkScalar shift() const { return fShift; }
  void shiftTo(SkScalar shift) { fShift = shift; }

  SkScalar alphabeticBaseline() const { return fSizes.alphabeticBaseline(); }
  SkScalar ideographicBaseline() const { return fSizes.ideographicBaseline(); }
  SkScalar baseline() const { return fSizes.baseline(); }

  SkRect measureTextInsideOneRun(SkSpan<const char> text,
                                 SkRun* run,
                                 size_t& pos,
                                 size_t& size,
                                 bool& clippingNeeded) const;
  SkVector measureWordAcrossAllRuns(SkSpan<const char> text) const;
  void justify(SkScalar maxWidth);
  void setEllipsis(std::unique_ptr<SkRun> ellipsis) { fEllipsis = std::move(ellipsis); }

  void iterateThroughStylesInTextOrder(
      SkStyleType styleType,
      SkSpan<SkBlock> blocks,
      bool checkOffsets,
      std::function<SkScalar(
          SkSpan<const char> text,
          const SkTextStyle& style,
          SkScalar offsetX)> apply) const;

  SkScalar iterateThroughRuns(
      SkSpan<const char> text,
      SkScalar offsetX,
      bool includeEmptyText,
      std::function<bool(SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded)> apply) const;

  void iterateThroughClustersInGlyphsOrder(
      bool reverse, std::function<bool(const SkCluster* cluster)> apply) const;

  void format(SkTextAlign effectiveAlign, SkScalar maxWidth, bool last);
  void paint(SkCanvas* canvas, SkSpan<SkBlock> blocks);
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

  void scanStyles(SkStyleType style, SkSpan<SkBlock> blocks,
                  std::function<void(SkTextStyle, SkSpan<const char>)> apply);
  void scanRuns(std::function<void(SkRun*, int32_t, size_t, SkRect)> apply);

 private:

  SkRun* shapeEllipsis(const std::string& ellipsis, SkRun* run);

  bool contains(const SkCluster* cluster) const {
    return cluster->text().begin() >= fText.begin() && cluster->text().end() <= fText.end();
  }

  SkSpan<const char> fText;
  SkSpan<SkCluster> fClusters;
  SkTArray<SkRun*, true> fLogical;
  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;  // Text on the line size
  SkVector fOffset;   // Text position on the screen
  std::unique_ptr<SkRun> fEllipsis;   // In case the line ends with the ellipsis
  SkLineMetrics fSizes;
  SkLineMetrics fStrutMetrics;

  static SkTHashMap<SkFont, SkRun> fEllipsisCache; // All found so far shapes of ellipsis
};

