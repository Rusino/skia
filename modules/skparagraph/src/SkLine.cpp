/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unicode/brkiter.h>
#include <algorithm>
#include "SkLine.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"

namespace {
  std::string toString2(SkSpan<const char> text) {
    icu::UnicodeString utf16 = icu::UnicodeString(text.begin(), text.size());
    std::string str;
    utf16.toUTF8String(str);
    return str;
  }
};

SkLine::SkLine() {
  fShift = 0;
  fAdvance.set(0, 0);
  fWidth = 0;
  fHeight = 0;
}

SkLine::SkLine(SkScalar width, SkScalar height, SkArraySpan<SkWords> words, SkArraySpan<SkRun> runs)
    : fUnbreakableWords(words)
    , fRuns(runs) {
  fAdvance = SkVector::Make(width, height);
  fOffset = runs.begin()->fInfo.fOffset;
  fHeight = height;
  fWidth = width;
  fText = SkSpan<const char>(
      words.begin()->text().begin(),
      words.back()->text().end() - words.begin()->text().begin()
      );
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

      fAdvance.fX = maxWidth;
      fShift = delta / 2;
      break;
    }
    case SkTextAlign::justify: {

      justify(delta);

      fShift = 0;
      fAdvance.fX = maxWidth;
      fWidth = maxWidth;
      break;
    }
    default:
      break;
  }
}

void SkLine::justify(SkScalar delta) {

  auto softLineBreaks = fUnbreakableWords.size() - 1;
  if (softLineBreaks == 0) {
    // Expand one group of words
    for (auto word = fUnbreakableWords.begin(); word != fUnbreakableWords.end(); ++word) {
      word->expand(delta);
    }
    return;
  }

  SkScalar step = delta / softLineBreaks;
  SkScalar shift = 0;

  SkWords* last = nullptr;
  for (auto word = fUnbreakableWords.begin(); word != fUnbreakableWords.end(); ++word) {

    if (last != nullptr) {
      --softLineBreaks;
      last->expand(step);
      shift += step;
    }

    last = word;
    word->shift(shift);
  }
}

void SkLine::iterateThroughRuns(
    SkSpan<const char> text,
    std::function<void(SkRun* run, int32_t pos, size_t size, SkRect clip)> apply) const {

  // Find the correct style positions (taking in account cluster limits)
  SkDebugf("iterateThroughRuns '%s'\n", toString2(text).c_str());
  auto startPos = SkRun::findPosition(SkSpan<SkRun>(fRuns.begin(), fRuns.size()), text.begin());
  auto endPos = SkRun::findPosition(SkSpan<SkRun>(fRuns.begin(), fRuns.size()), text.end());   // inclusive

  SkDebugf("%d:%d\n", startPos.fPos, endPos.fPos);
  for (auto& run = startPos.fRun; run <= endPos.fRun; ++run) {

    auto start = 0;
    auto size = run->size();

    SkRect clip = SkRect::MakeEmpty();
    if (run == startPos.fRun) {
      start = SkToU32(startPos.fPos);
      clip.fLeft = run->fPositions[start].fX + startPos.fShift;
      clip.fTop = run->fPositions[start].fY;
    }
    if (run == endPos.fRun) {
      size = SkToU32(endPos.fPos + 1);
      if (size == 0) {
        continue;
      }
      clip.fRight = run->fPositions[start].fX + run->fInfo.fAdvance.fX - endPos.fShift;
      clip.fBottom = run->fPositions[start].fY + run->calculateHeight();
    }

    SkDebugf("Clip: %f:%f %f:%f\n", clip.fLeft, clip.fRight, clip.fTop, clip.fBottom);
    apply(run, start, size, clip);
  }
}

// TODO: Justification dropped again for now. It's really gets in a way!
void SkLine::paintText(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const {

  // Build the blob from all the runs
  SkTextBlobBuilder builder;
  iterateThroughRuns(text,
      [&builder](SkRun* run, int32_t pos, size_t size, SkRect rect) {

        SkDebugf("blob %d:%d\n", pos, size);
        const auto& blobBuffer = builder.allocRunPos(run->fFont, SkToInt(size - pos));
        sk_careful_memcpy(blobBuffer.glyphs,
                          run->fGlyphs.data() + pos,
                          (size - pos) * sizeof(SkGlyphID));
        sk_careful_memcpy(blobBuffer.points(),
                          run->fPositions.data() + pos,
                          (size - pos) * sizeof(int32_t));
  });

  // Paint the blob with one foreground color
  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.reset();
    paint.setColor(style.getColor());
  }
  paint.setAntiAlias(true);
  canvas->drawTextBlob(builder.make(), 0, 0, paint);

}

void SkLine::paintBackground(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const {

  if (!style.hasBackground()) {
    return;
  }

  iterateThroughRuns(
      text,
      [canvas, style](SkRun* run, int32_t pos, size_t size, SkRect clip) {
        canvas->drawRect(clip, style.getBackground());
      });
}

void SkLine::paintShadow(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const {

  if (style.getShadowNumber() == 0) {
    return;
  }

  for (SkTextShadow shadow : style.getShadows()) {
    if (!shadow.hasShadow()) {
      continue;
    }

    SkPaint paint;
    paint.setColor(shadow.fColor);
    if (shadow.fBlurRadius != 0.0) {
      paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle,
                                                 shadow.fBlurRadius,
                                                 false));
    }

    SkTextBlobBuilder builder;
    iterateThroughRuns(text,
                       [&builder](SkRun* run, int32_t pos, size_t size, SkRect rect) {
                         const auto& blobBuffer = builder.allocRunPos(run->fFont, SkToInt(size - pos));
                         sk_careful_memcpy(blobBuffer.glyphs,
                                           run->fGlyphs.data() + pos,
                                           (size - pos) * sizeof(SkGlyphID));
                         sk_careful_memcpy(blobBuffer.points(),
                                           run->fPositions.data() + pos,
                                           (size - pos) * sizeof(int32_t));
                       });

    canvas->drawTextBlob(builder.make(),
                         shadow.fOffset.x(),
                         shadow.fOffset.y(),
                         paint);
  }
}

void SkLine::computeDecorationPaint(SkPaint& paint, SkRect clip, SkTextStyle textStyle, SkPath& path) const {

  paint.setStyle(SkPaint::kStroke_Style);
  if (textStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(textStyle.getColor());
  } else {
    paint.setColor(textStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);

  SkScalar scaleFactor = textStyle.getFontSize() / 14.f;

  switch (textStyle.getDecorationStyle()) {
    case SkTextDecorationStyle::kSolid:
      break;

    case SkTextDecorationStyle::kDouble:
      break;

      // Note: the intervals are scaled by the thickness of the line, so it is
      // possible to change spacing by changing the decoration_thickness
      // property of TextStyle.
    case SkTextDecorationStyle::kDotted: {
      const SkScalar intervals[] =
          {1.0f * scaleFactor, 1.5f * scaleFactor, 1.0f * scaleFactor,
           1.5f * scaleFactor};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, (int32_t) count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
      // Note: the intervals are scaled by the thickness of the line, so it is
      // possible to change spacing by changing the decoration_thickness
      // property of TextStyle.
    case SkTextDecorationStyle::kDashed: {
      const SkScalar intervals[] =
          {4.0f * scaleFactor, 2.0f * scaleFactor, 4.0f * scaleFactor,
           2.0f * scaleFactor};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, (int32_t) count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
    case SkTextDecorationStyle::kWavy: {

      int wave_count = 0;
      SkScalar x_start = 0;
      SkScalar wavelength = 2 * scaleFactor;
      auto width = clip.width();
      path.moveTo(0, 0);
      while (x_start + wavelength * 2 < width) {
        path.rQuadTo(wavelength,
                     wave_count % 2 != 0 ? wavelength : -wavelength,
                     wavelength * 2,
                     0);
        x_start += wavelength * 2;
        ++wave_count;
      }
      break;
    }
  }
}

// TODO: Make the thickness reasonable
void SkLine::paintDecorations(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle textStyle) const {

  if (textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  // Decoration thickness
  SkScalar thickness = textStyle.getDecorationThicknessMultiplier();

  // Decoration position
  SkScalar position;
  switch (textStyle.getDecoration()) {
    case SkTextDecoration::kUnderline:
      position = fBaseline + thickness;
      break;
    case SkTextDecoration::kOverline:
      position = thickness;
      break;
    case SkTextDecoration::kLineThrough: {
      position = (fBaseline - thickness) / 2;
      break;
    }
    default:
      position = 0;
      SkASSERT(false);
      break;
  }

  // Draw the decoration
  iterateThroughRuns(
      text,
      [this, canvas, textStyle, position, thickness](SkRun* run, int32_t pos, size_t size, SkRect clip) {
        auto width = clip.width();
        SkScalar x = clip.left();
        SkScalar y = clip.top() + position;

        // Decoration paint (for now) and/or path
        SkPaint paint;
        SkPath path;
        this->computeDecorationPaint(paint, clip, textStyle, path);
        paint.setStrokeWidth(thickness);

        switch (textStyle.getDecorationStyle()) {
          case SkTextDecorationStyle::kWavy:
            path.offset(x, y);
            canvas->drawPath(path, paint);
            break;
          case SkTextDecorationStyle::kDouble: {
            canvas->drawLine(x, y, x + width, y, paint);
            SkScalar bottom = y + thickness * 2;
            canvas->drawLine(x, bottom, x + width, bottom, paint);
            break;
          }
          case SkTextDecorationStyle::kDashed:
          case SkTextDecorationStyle::kDotted:
          case SkTextDecorationStyle::kSolid:
            canvas->drawLine(x,
                             y,
                             x + width,
                             y,
                             paint);
            break;
          default:
            break;
        }
      });
}