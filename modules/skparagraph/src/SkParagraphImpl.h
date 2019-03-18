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
#include "SkParagraph.h"
#include "SkPicture.h"
#include "SkBlock.h"
#include "SkTHash.h"

class SkCanvas;
class SkParagraphImpl final: public SkParagraph {
 public:

  SkParagraphImpl(const std::string& text,
              SkParagraphStyle style,
              std::vector<Block> blocks)
      : SkParagraph(text, style, blocks) {
    fTextStyles.reserve(blocks.size());
    for (auto& block : blocks) {
      fTextStyles.emplace_back(SkSpan<const char>(fUtf8.begin() + block.fStart, block.fEnd - block.fStart),
                               block.fStyle);
    }
  }

  SkParagraphImpl(const std::u16string& utf16text,
              SkParagraphStyle style,
              std::vector<Block> blocks)
      : SkParagraph(utf16text, style, blocks) {
    fTextStyles.reserve(blocks.size());
    for (auto& block : blocks) {
      fTextStyles.emplace_back(SkSpan<const char>(fUtf8.begin() + block.fStart, block.fEnd - block.fStart),
                               block.fStyle);
    }
  }

  ~SkParagraphImpl() override;

  bool layout(double width) override;

  void paint(SkCanvas* canvas, double x, double y) override;

  std::vector<SkTextBox> getRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rectHeightStyle,
      RectWidthStyle rectWidthStyle) override;

  SkPositionWithAffinity
  getGlyphPositionAtCoordinate(double dx, double dy) const override;

  SkRange<size_t> getWordBoundary(unsigned offset) override;

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

  void iterateThroughStyles(
      SkSpan<const char> text,
      SkStyleType styleType,
      std::function<void(SkSpan<const char> text, SkTextStyle style)> apply) const;
  void iterateThroughRuns(
      SkSpan<const char> text,
      std::function<void(const SkRun* run, size_t pos, size_t size, SkRect clip)> apply) const;
  void iterateThroughClusters(std::function<void(SkCluster& cluster, bool last)> apply);

  SkCluster* findCluster(const char* ch) const;
  SkVector measureText(SkSpan<const char> text) const;

  // Input
  SkTArray<SkBlock> fTextStyles;

  // Internal structures
  SkTHashMap<const char*, size_t> fIndexes;
  SkTArray<SkCluster> fClusters;
  SkTArray<SkRun, true> fRuns;
  SkTArray<SkLine, true> fLines;

  // Painting
  sk_sp<SkPicture> fPicture;
};