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

class SkTextBreaker {

 public:
  SkTextBreaker() : fPos(-1) {
  }

  bool initialize(SkSpan<const char> text, UBreakIteratorType type) {
    UErrorCode status = U_ZERO_ERROR;

    fSize = text.size();
    UText utf8UText = UTEXT_INITIALIZER;
    utext_openUTF8(&utf8UText, text.begin(), text.size(), &status);
    fAutoClose =
        std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>>(&utf8UText);
    if (U_FAILURE(status)) {
      SkDebugf("Could not create utf8UText: %s", u_errorName(status));
      return false;
    }
    fIterator = ubrk_open(type, "th", nullptr, 0, &status);
    if (U_FAILURE(status)) {
      SkDebugf("Could not create line break iterator: %s",
               u_errorName(status));
      SK_ABORT("");
    }

    ubrk_setUText(fIterator, &utf8UText, &status);
    if (U_FAILURE(status)) {
      SkDebugf("Could not setText on break iterator: %s",
               u_errorName(status));
      return false;
    }

    fPos = 0;
    return true;
  }

  size_t next(size_t pos) {
    fPos = ubrk_following(fIterator, SkToS32(pos));
    return eof() ?  fSize : fPos;
  }

  int32_t status() { return ubrk_getRuleStatus(fIterator); }

  bool eof() { return fPos == icu::BreakIterator::DONE; }

  ~SkTextBreaker() = default;

 private:
  std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> fAutoClose;
  UBreakIterator* fIterator;
  int32_t fPos;
  size_t fSize;
};

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

  SkVector measureText(SkSpan<const char> text) const;

 private:

  friend class SkParagraphBuilder;

  void resetContext();
  void buildClusterTable();
  void shapeTextIntoEndlessLine(SkSpan<const char> text, SkSpan<SkBlock> styles);
  SkRun* shapeEllipsis(SkRun* run);
  void markClustersWithLineBreaks();
  void breakShapedTextIntoLines(SkScalar maxWidth, size_t maxLines);
  void formatLinesByText(SkScalar maxWidth);
  void formatLinesByWords(SkScalar maxWidth);
  void justifyLine(SkLine& line, SkScalar maxWidth);
  void paintText(SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkRun* ellipsis) const;
  void paintBackground(SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkRun* ellipsis) const;
  void paintShadow(SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkRun* ellipsis) const;
  void paintDecorations(SkCanvas* canvas, SkSpan<const char> text, const SkTextStyle& style, SkRun* ellipsis) const;
  void computeDecorationPaint(SkPaint& paint, SkRect clip, const SkTextStyle& style, SkPath& path) const;

  void iterateThroughStyles(
      const SkLine& line,
      SkStyleType styleType,
      std::function<bool(SkSpan<const char> text, const SkTextStyle& style, SkRun* ellipsis)> apply) const;
  void iterateThroughRuns(
      SkSpan<const char> text,
      SkRun* ellipsis,
      std::function<bool(SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift)> apply) const;
  void iterateThroughClusters(std::function<bool(SkCluster& cluster, bool last)> apply);

  SkCluster* findCluster(const char* ch) const;

  SkRun* getEllipsis(SkRun* run);

  void addLine(SkVector offset, SkVector advance, SkSpan<const char> text, SkRun* ellipsis) {
    fLines.emplace_back(offset, advance, text, ellipsis);
    fWidth =  SkMaxScalar(fWidth, advance.fX);
    fHeight += advance.fY;
  }

  bool reachesLinesLimit() {
    return !fParagraphStyle.unlimited_lines() &&
                fLines.size() >= fParagraphStyle.getMaxLines();
  }

  bool didExceedMaxLines() override {
    return !fParagraphStyle.unlimited_lines()
        && fLines.size() > fParagraphStyle.getMaxLines();
  }

  // Input
  SkTArray<SkBlock> fTextStyles;

  // Internal structures
  SkTHashMap<const char*, size_t> fIndexes;
  SkTArray<SkCluster> fClusters;
  SkTArray<SkRun, true> fRuns;
  SkTArray<SkLine, true> fLines;
  SkTHashMap<SkFont, SkRun> fEllipsis; // All found so far shapes of ellipsis

  // Painting
  sk_sp<SkPicture> fPicture;
};