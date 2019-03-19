/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkDartTypes.h"
#include "SkSpan.h"
#include "SkTArray.h"
#include "SkRun.h"

class SkWord {

 public:

  SkWord() { }

  SkWord(SkSpan<const char> text)
      : fText(text)
      , fShift(0)
      , fAdvance(SkVector::Make(0, 0)) { }

  inline SkSpan<const char> text() const { return fText; }
  inline SkVector advance() const { return fAdvance; }
  inline SkScalar offset() const { return fShift; }
  inline void shift(SkScalar shift) { fShift += shift; }
  inline void expand(SkScalar step) { fAdvance.fX += step; }
  inline bool empty() const { return fText.empty(); }

 private:

  friend class SkParagraphImpl;

  SkSpan<const char> fText;
  SkScalar fShift;    // For justification
  SkVector fAdvance;  // Size
};

class SkLine {

 public:

  SkLine() { }

  SkLine(SkVector offset, SkVector advance, SkSpan<const char> text, SkRun* ellipsis)
      : fText(text)
      , fShift(0)
      , fAdvance(advance)
      , fOffset(offset)
      , fEllipsis(ellipsis) { }

  inline SkSpan<const char> text() const { return fText; }
  inline SkVector advance() const { return fAdvance; }
  inline SkVector offset() const { return fOffset + SkVector::Make(fShift, 0); }
  inline bool empty() const { return fText.empty(); }

  void formatByWords(SkTextAlign align, SkScalar maxWidth);
  void breakLineByWords(std::function<void(SkWord& word)> apply);

 private:

  friend class SkParagraphImpl;

  void justify(SkScalar delta);

  SkSpan<const char> fText;
  SkScalar fShift;    // Shift to left - right - center
  SkVector fAdvance;  // Text on the line size
  SkVector fOffset;
  SkTArray<SkWord, true> fWords;
  SkRun* fEllipsis;   // In case the line ends with the ellipsis
};

