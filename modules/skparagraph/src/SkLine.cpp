/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unicode/brkiter.h>
#include <algorithm>
#include "SkLine.h"
#include "SkParagraphImpl.h"

void SkLine::formatByWords(SkTextAlign effectiveAlign, SkScalar maxWidth) {
  SkScalar delta = maxWidth - fAdvance.fX;
  if (delta <= 0) {
    // Delta can be < 0 if there are extra whitespaces at the end of the line;
    // This is a limitation of a current version
    return;
  }

  switch (effectiveAlign) {
    case SkTextAlign::left:

      fShift = 0;
      break;
    case SkTextAlign::right:

      fAdvance.fX = maxWidth;
      fShift = delta;
      break;
    case SkTextAlign::center: {

      fAdvance.fX = maxWidth;
      fShift = delta / 2;
      break;
    }
    case SkTextAlign::justify: {

      justify(delta);

      fShift = 0;
      fAdvance.fX = maxWidth;
      break;
    }
    default:
      break;
  }
}

void SkLine::justify(SkScalar delta) {

  auto softLineBreaks = fWords.size() - 1;
  if (softLineBreaks == 0) {
    // Expand one group of words
    for (auto word = fWords.begin(); word != fWords.end(); ++word) {
      word->expand(delta);
    }
    return;
  }

  SkScalar step = delta / softLineBreaks;
  SkScalar shift = 0;

  SkWord* last = nullptr;
  for (auto word = fWords.begin(); word != fWords.end(); ++word) {

    if (last != nullptr) {
      --softLineBreaks;
      last->expand(step);
      shift += step;
    }

    last = word;
    word->shift(shift);
  }
}

void SkLine::breakLineByWords(std::function<void(SkWord& word)> apply) {

  SkTextBreaker breaker;
  if (!breaker.initialize(fText, UBRK_WORD)) {
    return;
  }

  size_t currentPos = 0;
  while (!breaker.eof()) {
    auto start = currentPos;
    currentPos = breaker.next(currentPos);

    SkSpan<const char> text(fText.begin() + start, currentPos - start);
    fWords.emplace_back(text);

    apply(fWords.back());
  }
}