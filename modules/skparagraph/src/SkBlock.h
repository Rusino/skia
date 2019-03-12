/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <SkTDPQueue.h>
#include "SkCanvas.h"
#include "SkSpan.h"
#include "SkScalar.h"
#include "SkShaper.h"
#include "SkTextStyle.h"
#include "SkArraySpan.h"
#include "SkRun.h"

template<typename T>
inline bool operator==(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.size() == b.size() && a.begin() == b.begin();
}

template<typename T>
inline bool operator<=(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.begin() >= b.begin() && a.end() <= b.end();
}

template<typename T>
inline bool operator&&(const SkSpan<T>& a, const SkSpan<T>& b) {
  return a.end() > b.begin() && a.begin() < b.end();
}

class SkBlock {
 public:

  SkBlock() : fText(), fTextStyle() {}
  SkBlock(SkSpan<const char> text, SkTextStyle style)
      : fText(text), fTextStyle(style) {}

  inline SkSpan<const char> text() const { return fText; }
  inline SkTextStyle style() const { return fTextStyle; }

 protected:
  SkSpan<const char> fText;
  SkTextStyle fTextStyle;
};

// A set of "unbreakable" words - they do not break glyph clusters
// We can always break a line before or after this group
class SkWords {
 public:
  SkWords(SkSpan<const char> text,
          SkSpan<const char> spaces,
          SkTArray<size_t, true> words)
      : fText(text), fTrailingSpaces(spaces), fWords(std::move(words)) {}

  SkWords(SkSpan<const char> text)
      : fText(text), fTrailingSpaces(SkSpan<const char>()), fWords() {}

  inline bool isProducedByShaper() { return fProducedByShaper; }
  bool hasTrailingSpaces();
  SkScalar trim();
  //inline SkSpan<SkWord> words() const { return fWords; }
  inline SkScalar trimmedWidth() const { return fTrimmedWidth; }
  inline void setTrimmedWidth(SkScalar tw) { fTrimmedWidth = tw; }
  inline SkScalar spaceWidth() const { return fAdvance.fX - fTrimmedWidth; }
  inline SkScalar width() const { return fAdvance.fX; }
  inline SkScalar height() const { return fAdvance.fY; }
  inline SkSpan<const char> text() const { return fText; }
  inline void setAdvance(SkVector advance) { fAdvance = advance; }
  inline void setAdvance(SkScalar width, SkScalar height) { fAdvance =
                                                                SkVector::Make(
                                                                    width,
                                                                    height);
  }

  void shift(SkScalar shift) { fOffset.offset(shift, 0); }
  void expand(SkScalar step) { fAdvance.fX += step; }

  void getRectsForRange(
      SkTextDirection textDirection,
      const char* start,
      const char* end,
      std::vector<SkTextBox>& result) {

    // TODO: implement
  }

 private:
  SkVector fOffset;
  SkVector fAdvance;
  SkScalar fTrimmedWidth;
  SkSpan<const char> fText;
  SkSpan<const char> fTrailingSpaces;
  SkTArray<size_t, true> fWords;
  bool fProducedByShaper;
};

class SkStyle : public SkBlock {

 public:

  SkStyle(SkSpan<const char> text, SkTextStyle style)
      : SkBlock(text, style), fClip(SkRect::MakeEmpty()) {}

  SkStyle(SkSpan<const char> text,
          SkTextStyle style,
          sk_sp<SkTextBlob> blob,
          SkRect clip)
      : SkBlock(text, style), fTextBlob(blob), fClip(clip) {}


  inline sk_sp<SkTextBlob> blob() const { return fTextBlob; }
  inline SkRect clip() const { return fClip; }
  inline SkScalar width() const { return fClip.width(); }
  inline SkScalar height() const { return fClip.height(); }

 private:

  friend class SkSection;
  friend class SkLine;
  friend class MultipleFontRunIterator;

  sk_sp<SkTextBlob> fTextBlob;
  SkRect fClip;
};
