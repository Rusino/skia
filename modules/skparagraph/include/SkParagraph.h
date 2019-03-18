/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"
#include "SkLine.h"
#include "SkRun.h"

class SkCanvas;
class SkSection;
class SkPicture;

class SkParagraph {
 private:
  struct Block {
    Block(size_t start, size_t end, SkTextStyle style)
        : fStart(start), fEnd(end), fStyle(style) {}
    size_t fStart;
    size_t fEnd;
    SkTextStyle fStyle;
  };

 public:
  SkParagraph(
      const std::u16string& utf16text,
      SkParagraphStyle style,
      std::vector<Block> blocks);

  SkParagraph(
      const std::string& utf8text,
      SkParagraphStyle style,
      std::vector<Block> blocks);

  ~SkParagraph();

  double getMaxWidth() { return SkScalarToDouble(fWidth); }

  double getHeight() { return SkScalarToDouble(fHeight); }

  double getMinIntrinsicWidth() { return SkScalarToDouble(fMinIntrinsicWidth); }

  double getMaxIntrinsicWidth() { return SkScalarToDouble(fMaxIntrinsicWidth); }

  double
  getAlphabeticBaseline() { return SkScalarToDouble(fAlphabeticBaseline); }

  double
  getIdeographicBaseline() { return SkScalarToDouble(fIdeographicBaseline); }

  bool didExceedMaxLines() {

    return !fParagraphStyle.unlimited_lines()
        && fLinesNumber > fParagraphStyle.getMaxLines();
  }

  bool layout(double width);

  void paint(SkCanvas* canvas, double x, double y);

  std::vector<SkTextBox> getRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rectHeightStyle,
      RectWidthStyle rectWidthStyle);

  SkPositionWithAffinity
  getGlyphPositionAtCoordinate(double dx, double dy) const;

  SkRange<size_t> getWordBoundary(unsigned offset);

 private:

  friend class SkParagraphBuilder;

  void resetContext();
  void buildClusterTable();
  void shapeTextIntoEndlessLine();
  void markClustersWithLineBreaks();
  void shapeIntoLines(SkScalar maxWidth, size_t maxLines);
  void breakShapedTextIntoLinesByClusters(SkScalar maxWidth,
                                          size_t maxLines);
  void formatLinesByWords(SkScalar maxWidth);
  void paintText(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void paintBackground(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void paintShadow(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void paintDecorations(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const;
  void computeDecorationPaint(SkPaint& paint, SkRect clip, SkTextStyle style, SkPath& path) const;

  size_t linesLeft() { return fParagraphStyle.unlimited_lines()
                                ? fParagraphStyle.getMaxLines()
                                : fParagraphStyle.getMaxLines()  - fLinesNumber; }

  bool addLines(size_t increment) {
    fLinesNumber += increment;
    return fLinesNumber < fParagraphStyle.getMaxLines();
  }

  void iterateThroughStyles(
      SkSpan<const char> text,
      SkStyleType styleType,
      std::function<void(SkSpan<const char> text, SkTextStyle style)> apply) const;
  void iterateThroughRuns(
      SkSpan<const char> text,
      std::function<void(const SkRun* run, size_t pos, size_t size, SkRect clip)> apply) const;
  void iterateThroughClusters(std::function<void(SkCluster& cluster, bool last)> apply);

  size_t findCluster(const char* ch) const;
  SkVector measureText(SkSpan<const char> text) const;
  void measureWords(SkWords& words) const;
  SkScalar findOffset(const char* ch) const;

  // Things for Flutter
  SkScalar fAlphabeticBaseline;
  SkScalar fIdeographicBaseline;
  SkScalar fHeight;
  SkScalar fWidth;
  SkScalar fMaxIntrinsicWidth;
  SkScalar fMinIntrinsicWidth;
  SkScalar fMaxLineWidth;
  size_t fLinesNumber;

  // Input
  SkParagraphStyle fParagraphStyle;
  SkTArray<SkBlock> fTextStyles;
  SkSpan<const char> fUtf8;
  // TODO: later
  //SkTArray<SkWords, true> fUnbreakableWords;

  // Internal structures
  SkTArray<SkRun, true> fRuns;
  SkTArray<SkLine, true> fLines;
  SkTArray<SkCluster, true> fClusters;

  // Painting
  sk_sp<SkPicture> fPicture;
};