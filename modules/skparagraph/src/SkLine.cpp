/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unicode/brkiter.h>
#include <algorithm>
#include "SkLine.h"
/*
namespace {
  std::string toString2(SkSpan<const char> text) {
    icu::UnicodeString utf16 = icu::UnicodeString(text.begin(), text.size());
    std::string str;
    utf16.toUTF8String(str);
    return str;
  }
};
*/
SkLine::SkLine() {
  fShift = 0;
  fAdvance.set(0, 0);
  fWidth = 0;
  fHeight = 0;
}

SkLine::SkLine(SkVector advance, SkScalar baseline, SkSpan<StyledText> styles, SkArraySpan<SkWord> words)
    : fTextStyles(styles)
    , fWords(words) {
  fAdvance = advance;
  fWords = words;
  fHeight = advance.fY;
  fWidth = advance.fX;
  fBaseline = baseline;
}

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
      fAdvance.fX = fWidth;
      break;
    case SkTextAlign::right:
      fAdvance.fX = maxWidth;
      fShift = delta;
      break;
    case SkTextAlign::center: {
      auto half = delta / 2;
      fAdvance.fX = maxWidth;
      fShift = half;
      break;
    }
    case SkTextAlign::justify: {

      fShift = 0;
      fAdvance.fX = maxWidth;
      fWidth = maxWidth;

      auto softLineBreaks = std::count_if(fWords.begin(), fWords.end(), [](SkWord word){return word.fMayLineBreakBefore; });
      if (fWords.begin()->fMayLineBreakBefore) {
        --softLineBreaks;
      }

      if (softLineBreaks == 0) {
        // Expand one group of words
        for (auto word = fWords.begin(); word != fWords.end(); ++word) {
          word->expand(delta);
        }
        break;
      }

      SkScalar step = delta / softLineBreaks;
      SkScalar shift = 0;

      SkWord* last = nullptr;
      for (auto word = fWords.begin(); word != fWords.end(); ++word) {

        if (word->fMayLineBreakBefore && last != nullptr) {
          --softLineBreaks;
          last->expand(step);
          shift += step;
        }

        last = word;
        word->shift(shift);
      }
      break;
    }
    default:
      break;
  }
}

// TODO: For now we paint everything by words but we better combine words by style
void SkLine::paintByStyles(SkCanvas* canvas) {

  if (fWords.empty()) {
    return;
  }

  auto offsetX = fWords.begin()->offset().fX;

  canvas->save();
  canvas->translate(fShift - offsetX, 0);

  generateWordTextBlobs(offsetX);

  paintBackground(canvas);

  paintShadow(canvas);

  paintDecorations(canvas);

  paintText(canvas);
  canvas->restore();
}

void SkLine::generateWordTextBlobs(SkScalar offsetX) {

  for (auto word = fWords.begin(); word != fWords.end(); ++word) {

    word->generate();
    word->dealWithStyles(fTextStyles);
  }
}

void SkLine::paintBackground(SkCanvas* canvas) {

  for (auto word = fWords.begin(); word != fWords.end(); ++word) {
    word->paintBackground(canvas);
  }
}

void SkLine::paintShadow(SkCanvas* canvas) {

  for (auto word = fWords.begin(); word != fWords.end(); ++word) {
    word->paintShadow(canvas);
  }
}

void SkLine::paintDecorations(SkCanvas* canvas) {

  for (auto word = fWords.begin(); word != fWords.end(); ++word) {
    word->paintDecorations(canvas, fBaseline);
  }
}

void SkLine::paintText(SkCanvas* canvas) {

  for (auto word = fWords.begin(); word != fWords.end(); ++word) {
    word->paint(canvas);
  }
}

void SkLine::getRectsForRange(
    SkTextDirection textDirection,
    const char* start,
    const char* end,
    std::vector<SkTextBox>& result) {

  for (auto word = fWords.begin(); word != fWords.end(); ++word) {
    if (word->text().end() <= start || word->text().begin() >= end) {
      continue;
    }
    result.emplace_back(word->rect(), textDirection);
  }
}
