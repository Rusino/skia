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

inline bool operator&&(const SkSpan<const char>& a, const SkSpan<const char>& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  return SkTMax(a.begin(), b.begin()) < SkTMin(a.end(), b.end());
}

class SkBlock {
 public:

  SkBlock() : fText(), fTextStyle() {}
  SkBlock(SkSpan<const char> text, SkTextStyle* style)
      : fText(text), fTextStyle(style) {}

  inline SkSpan<const char> text() const { return fText; }
  inline SkTextStyle style() const { return *fTextStyle; }

 protected:
  SkSpan<const char> fText;
  SkTextStyle* fTextStyle;
};

// A set of "unbreakable" words - they do not break glyph clusters
// We can always break a line before or after this group
class SkWords {
 public:
  SkWords(SkSpan<const char> text,
          SkSpan<const char> spaces)
      : fText(text)
      , fTrailingSpaces(spaces)
      , fTrimmed(false) {}

  SkWords(const SkRun& run)
      : fText(run.text())
      , fTrailingSpaces(SkSpan<const char>())
      , fTrimmed(false) {
    fProducedBy = &run;
    fAdvance = run.advance();
    fOffset = SkVector::Make(0, 0);
    fTrimmedWidth = fAdvance.fX;
  }

  inline bool isProducedByShaper() { return fProducedByShaper; }
  bool hasTrailingSpaces() { return !fTrailingSpaces.empty(); }
  void trim() {
    fAdvance.fX = fTrimmedWidth;
    fTrimmed = true;
  }

  inline SkScalar width() const { return fAdvance.fX; }
  inline SkScalar height() const { return fAdvance.fY; }
  inline SkScalar trimmedWidth() const { return fTrimmedWidth; }
  inline SkScalar spaceWidth() const { return fAdvance.fX - fTrimmedWidth; }
  inline SkSpan<const char> trimmed() const { return fText; }
  inline SkSpan<const char> full() const {
    if (fTrimmed) {
      return fText;
    }
    return SkSpan<const char>(fText.begin(), fText.size() + fTrailingSpaces.size());
  }
  inline SkSpan<const char> spaces() const { return fTrailingSpaces; }

  void shift(SkScalar shift) { fOffset.offset(shift, 0); }
  void expand(SkScalar step) { fAdvance.fX += step; }
  void setSizes(SkVector advance, SkScalar trimmedWidth) {
    fAdvance = advance;
    fTrimmedWidth = trimmedWidth;
  }
  const SkRun* producedBy() { return fProducedBy; }

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
  bool fTrimmed;
  bool fProducedByShaper;
  const SkRun* fProducedBy;
};

class SkStyle : public SkBlock {

 public:

  SkStyle(SkSpan<const char> text, SkTextStyle* style)
      : SkBlock(text, style), fClip(SkRect::MakeEmpty()) {}

  SkStyle(SkSpan<const char> text,
          SkTextStyle* style,
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
