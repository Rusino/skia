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
#include "SkTHash.h"
#include "SkTextWrapper.h"

template<typename T>
inline bool operator==(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.size() == b.size() && a.begin() == b.begin();
}

template<typename T>
inline bool operator<=(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.begin() >= b.begin() && a.end() <= b.end();
}

inline bool operator&&(const SkSpan<const char>& a, const SkSpan<const char>& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  return SkTMax(a.begin(), b.begin()) < SkTMin(a.end(), b.end());
}

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
    fIterator = ubrk_open(type, "en", nullptr, 0, &status);
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

  size_t first() {
    fPos = ubrk_first(fIterator);
    return eof() ?  fSize : fPos;
  }

  size_t next() {
    fPos = ubrk_next(fIterator);
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
                  std::vector<Block> blocks,
                  sk_sp<SkFontCollection> fonts)
      : SkParagraph(text, style, std::move(fonts))
      , fTextWrapper(this)
      , fPicture(nullptr) {
    fTextStyles.reserve(blocks.size());
    for (auto& block : blocks) {
      fTextStyles.emplace_back(SkSpan<const char>(fUtf8.begin() + block.fStart, block.fEnd - block.fStart),
                               block.fStyle);
    }
  }

  SkParagraphImpl(const std::u16string& utf16text,
                  SkParagraphStyle style,
                  std::vector<Block> blocks,
                  sk_sp<SkFontCollection> fonts)
      : SkParagraph(utf16text, style, std::move(fonts))
      , fTextWrapper(this)
      , fPicture(nullptr) {
    fTextStyles.reserve(blocks.size());
    for (auto& block : blocks) {
      fTextStyles.emplace_back(SkSpan<const char>(fUtf8.begin() + block.fStart, block.fEnd - block.fStart),
                               block.fStyle);
    }
  }

  ~SkParagraphImpl() override;

  void layout(SkScalar width) override;
  void paint(SkCanvas* canvas, SkScalar x, SkScalar y) override;
  std::vector<SkTextBox> getRectsForRange(
      unsigned start,
      unsigned end,
      RectHeightStyle rectHeightStyle,
      RectWidthStyle rectWidthStyle) override;
  SkPositionWithAffinity
  getGlyphPositionAtCoordinate(SkScalar dx, SkScalar dy) override;
  SkRange<size_t> getWordBoundary(unsigned offset) override;
  bool didExceedMaxLines() override {
    return !fParagraphStyle.unlimited_lines()
        && fLines.size() > fParagraphStyle.getMaxLines();
  }

  SkLine& addLine (SkVector offset, SkVector advance, SkSpan<const char> text, SkFontSizes sizes);

  inline SkSpan<const char> text() const { return fUtf8; }

 private:

  friend class SkParagraphBuilder;

  void resetContext();
  void buildClusterTable();
  void shapeTextIntoEndlessLine();
  void breakShapedTextIntoLines(SkScalar maxWidth);
  void formatLinesByWords(SkScalar maxWidth);
  void paintLinesIntoPicture();

  // Input
  SkTArray<SkBlock, true> fTextStyles;

  // Internal structures
  SkTArray<SkRun> fRuns;
  SkTArray<SkCluster, true> fClusters;
  SkTArray<SkLine> fLines;
  SkTextWrapper fTextWrapper;

  // Painting
  sk_sp<SkPicture> fPicture;
};
