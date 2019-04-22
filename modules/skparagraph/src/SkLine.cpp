/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unicode/brkiter.h>
#include <algorithm>
#include <unicode/ubidi.h>
#include "SkLine.h"
#include "SkParagraphImpl.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "SkMaskFilter.h"

namespace {

std::string toString(SkSpan<const char> text) {
  icu::UnicodeString
      utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
  std::string str;
  utf16.toUTF8String(str);
  return str;
}
/*
  void print(const std::string& label, const SkCluster* cluster) {
    SkDebugf("%s[%d:%f]: '%s'\n",
        label.c_str(),
        cluster->fStart,
        cluster->fRun->position(cluster->fStart).fX, toString(cluster->fText).c_str());
  }
 */
SkSpan<const char> operator*(const SkSpan<const char>& a, const SkSpan<const char>& b) {
  auto begin = SkTMax(a.begin(), b.begin());
  auto end = SkTMin(a.end(), b.end());
  return SkSpan<const char>(begin, end > begin ? end - begin : 0);
}

}

SkLine::SkLine(const SkLine& other) {
  this->fText = other.fText;
  this->fWords.reset();
  this->fWords = std::move(other.fWords);
  this->fLogical.reset();
  this->fLogical = std::move(other.fLogical);
  this->fShift = other.fShift;
  this->fAdvance = other.fAdvance;
  this->fWidth = other.fWidth;
  this->fOffset = other.fOffset;
  this->fEllipsis.reset(other.fEllipsis == nullptr ? nullptr : new SkRun(*other.fEllipsis));
  this->fSizes = other.sizes();
  this->fClusters = other.fClusters;
  this->fLeftToRight = other.fLeftToRight;
}

void SkLine::breakLineByWords(UBreakIteratorType type, std::function<void(SkWord& word)> apply) {

  // TODO: do not create more breakers that we have to
  SkTextBreaker breaker;
  if (!breaker.initialize(fText, type)) {
    return;
  }
  fWords.reset();
  size_t currentPos = breaker.first();
  while (true) {
    auto start = currentPos;
    currentPos = breaker.next();
    if (breaker.eof()) {
      break;
    }
    SkSpan<const char> text(fText.begin() + start, currentPos - start);
    fWords.emplace_back(text);
    apply(fWords.back());
  }
}

void SkLine::reorderRuns() {

  auto start = fClusters.begin();
  auto end = fClusters.end() - 1;
  size_t numRuns = end->fRun->index() - start->fRun->index() + 1;

  // Get the logical order
  std::vector<UBiDiLevel> runLevels;
  for (auto run = start->fRun; run <= end->fRun; ++run) {
    runLevels.emplace_back(run->fBidiLevel);
  }

  std::vector<int32_t> logicals(numRuns);
  ubidi_reorderVisual(runLevels.data(), SkToU32(numRuns), logicals.data());

  auto firstRun = start->fRun;
  for (auto logical : logicals) {
    fLogical.push_back(firstRun + logical);
  }
}

// The text must be shaped within one single run
// TODO: optimize
SkVector SkLine::measureText(SkSpan<const char> text) const {

  SkVector size = SkVector::Make(0, 0);
  if (text.empty()) {
    return size;
  }

  this->iterateThroughRuns(text, 0,
    [&](SkRun* run, int32_t p, size_t s, SkRect clip, SkScalar shift) {

      size.fX += clip.width();
      size.fY = SkTMax(size.fY, clip.height());
      return true;
    });
/*
  auto start = text.begin();
  auto end = text.end() - 1;
  for (auto& cluster : fClusters) {

    if (cluster.fText.begin() >= text.end()) {
      continue;
    } else if (cluster.fText.end() <= text.begin()) {
      break;
    }

    if (cluster.fText.begin() <= start && cluster.fText.end() > start) {
      size.fX -= cluster.sizeToChar(start);
    }
    if (cluster.fText.begin() < end && cluster.fText.end() >= end) {
      size.fX += cluster.sizeFromChar(end);
    } else {
      size.fX += cluster.fWidth;
    }

    size.fY = SkTMax(size.fY, cluster.fHeight);
  }
*/
  return size;
}

// TODO: Implement justification correctly, but only if it's needed
void SkLine::justify(SkScalar maxWidth) {

  SkScalar len = 0;
  this->breakLineByWords(UBRK_LINE, [this, &len](SkWord& word) {
    word.fAdvance = this->measureText(word.text());
    word.fShift = len;
    len += word.fAdvance.fX;
    return true;
  });

  auto delta = maxWidth - len;
  auto softLineBreaks = this->fWords.size() - 1;
  if (softLineBreaks == 0) {
    auto word = this->fWords.begin();
    word->expand(delta);
    this->fShift = 0;
    this->fAdvance.fX = maxWidth;
    return;
  }

  SkScalar step = delta / softLineBreaks;
  SkScalar shift = 0;

  SkWord* last = nullptr;
  for (auto& word : this->fWords) {

    if (last != nullptr) {
      --softLineBreaks;
      last->expand(step);
      shift += step;
    }

    last = &word;
    word.shift(shift);
    // Correct all runs and position for all the glyphs in the word
    this->iterateThroughRuns(word.text(), false,
                             [shift, word](SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar) {
                               SkDebugf("Word '%s': +%f =%f\n", toString(word.text()).c_str(), shift, clip.width());
                               for (auto i = pos; i < pos + size; ++i) {
                                 run->fOffsets[i] = shift;
                               }
                               //run->fAdvance.fX += shift;
                               run->fJustified = true;
                               return true;
                             });
  }

  this->fShift = 0;
  this->fAdvance.fX = maxWidth;
  this->fWidth = maxWidth;
}

SkRect SkLine::measureText(SkSpan<const char> text, SkRun* run, size_t& pos, size_t& size) const {

  auto first = text.begin();
  auto last = text.end() - 1;

  SkCluster* start = nullptr;
  SkCluster* end = nullptr;

  for (auto& cluster : fClusters) {
    if (cluster.fText.begin() <= first && cluster.fText.end() >= first) {
      start = &cluster;
    }
    if (cluster.fText.begin() <= last && cluster.fText.end() >= last) {
      end = &cluster;
    }
  }
  SkASSERT(start != nullptr && end != nullptr);
  if (!run->leftToRight()) {
    std::swap(start, end);
  }

  auto lineOffset = run->position(0).fX;
  size = end->fEnd - start->fStart;
  pos = start->fStart;
  SkRect clip = SkRect::MakeXYWH( run->position(start->fStart).fX - lineOffset,
                                  run->sizes().diff(sizes()),
                                  run->calculateWidth(start->fStart, end->fEnd),
                                  run->calculateHeight());

  // Correct the width in case the text edges don't match clusters
  if (start->fText.begin() <= first && start->fText.end() > first) {
    auto diff = start->sizeToChar(first);
    clip.fLeft += diff;
  }
  if (end->fText.begin() <= last && end->fText.end() > last) {
    auto diff = end->sizeFromChar(last);
    clip.fRight -= diff;
  }

  return clip;
}

// TODO: optimize
SkScalar SkLine::iterateThroughRuns(
    SkSpan<const char> text,
    SkScalar runOffset,
    std::function<void(SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift)> apply) const {

  if (text.empty()) {
    return 0;
  }

  SkScalar width = 0;
  if (!fLeftToRight && this->ellipsis() != nullptr) {
    auto ellipsis = this->ellipsis();
    apply(ellipsis, 0, ellipsis->size(), ellipsis->clip(), ellipsis->clip().fLeft);
    runOffset += ellipsis->advance().fX;
    width += ellipsis->advance().fX;
  }

  // Walk through the runs in the logical order
  for (auto run : fLogical) {

    // Find the intersection between the text and the run
    SkSpan<const char> intersect = run->text() * text;
    if (intersect.empty()) {
      continue;
    }

    size_t pos;
    size_t size;
    SkRect clip = this->measureText(intersect, run, pos, size);

    auto lineOffset = run->position(0).fX;
    auto shift1 = runOffset - clip.fLeft;
    auto shift2 = runOffset - clip.fLeft - lineOffset;
    clip.offset(shift1, 0);
    //SkDebugf("%f -%f - %f = %f (%f)\n", runOffset, clip.fLeft, lineOffset, shift2, clip.width());
    apply(run, pos, size, clip, shift2);

    width += clip.width();
    runOffset += clip.width();
  }

  // TODO: calculate the ellipse for the last visual run
  if (fLeftToRight && this->ellipsis() != nullptr) {
    auto ellipsis = this->ellipsis();
    apply(ellipsis, 0, ellipsis->size(), ellipsis->clip(), ellipsis->clip().fLeft);
  }

  return width;
}

void SkLine::iterateThroughStyles(
    SkStyleType styleType,
    SkSpan<SkBlock> blocks,
    std::function<SkScalar(
        SkSpan<const char> text,
        const SkTextStyle& style,
        SkScalar offsetX)> apply) const {

  const char* start = nullptr;
  size_t size = 0;
  SkTextStyle prevStyle;

  SkScalar offsetX = 0;
  for (auto& block : blocks) {

    auto intersect = block.text() * this->text();
    if (intersect.empty()) {
      if (start == nullptr) {
        // This style is not applicable to the line
        continue;
      } else {
        // We have found all the good styles already
        break;
      }
    }

    auto style = block.style();
    //auto begin = SkTMax(block.text().begin(), this->text().begin());
    //auto end = SkTMin(block.text().end(), this->text().end());
    //auto intersect = SkSpan<const char>(begin, end - begin);
    if (start != nullptr && style.matchOneAttribute(styleType, prevStyle)) {
      size += intersect.size();
      continue;
    } else if (size == 0) {
      // First time only
      prevStyle = style;
      size = intersect.size();
      start = intersect.begin();
      continue;
    }

    auto text = SkSpan<const char>(start, size);
    SkDebugf("Apply style @%f '%s'\n", offsetX, toString(text).c_str());
    auto width = apply(text, prevStyle, offsetX);
    SkDebugf("Width: %f + %f\n", offsetX, width);
    offsetX += width;
    // Start all over again
    prevStyle = style;
    start = intersect.begin();
    size = intersect.size();
  }

  // The very last style
  auto text = SkSpan<const char>(start, size);
  auto width = apply(text, prevStyle, offsetX);
  offsetX += width;
  if (offsetX != this->width()) {
    //SkDebugf("!!!\n");
  }
}


SkScalar SkLine::paintText(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkScalar offsetX) const {

  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.setColor(style.getColor());
  }

  return this->iterateThroughRuns(text, offsetX,
    [paint, canvas, this](SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift) {

      SkTextBlobBuilder builder;
      run->copyTo(builder, SkToU32(pos), size, SkVector::Make(0, this->sizes().leading() / 2 - this->sizes().ascent()));
      canvas->save();
      canvas->clipRect(clip);
      canvas->translate(shift, 0);
      canvas->drawTextBlob(builder.make(), 0, 0, paint);
      canvas->restore();
      return true;
    });
}

SkScalar SkLine::paintBackground(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkScalar offsetX) const {

  if (!style.hasBackground()) {
    // Still need to calculate text advance
    return iterateThroughRuns(text, offsetX,
                              [](SkRun*, int32_t, size_t, SkRect, SkScalar) { return true; });
  }
  return this->iterateThroughRuns(
      text, offsetX,
      [canvas, style](SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift) {
        canvas->drawRect(clip, style.getBackground());
        return true;
      });
}

SkScalar SkLine::paintShadow(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkScalar offsetX) const {

  if (style.getShadowNumber() == 0) {
    // Still need to calculate text advance
    return iterateThroughRuns(text, offsetX,
                              [](SkRun* run, int32_t, size_t, SkRect, SkScalar) { return true; });
  }

  SkScalar result;
  for (SkTextShadow shadow : style.getShadows()) {

    if (!shadow.hasShadow()) continue;

    SkPaint paint;
    paint.setColor(shadow.fColor);
    if (shadow.fBlurRadius != 0.0) {
      auto filter = SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, SkDoubleToScalar(shadow.fBlurRadius), false);
      paint.setMaskFilter(filter);
    }

    result = this->iterateThroughRuns(text, offsetX,
      [canvas, shadow, paint, this](SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift) {
        SkTextBlobBuilder builder;
        run->copyTo(builder, pos, size, SkVector::Make(0, this->sizes().leading() / 2 - this->sizes().ascent()));
        canvas->save();
        clip.offset(shadow.fOffset);
        canvas->clipRect(clip);
        canvas->translate(shift, 0);
        canvas->drawTextBlob(builder.make(), shadow.fOffset.x(), shadow.fOffset.y(), paint);
        canvas->restore();
        return true;
      });
  }

  return result;
}

void SkLine::computeDecorationPaint(
    SkPaint& paint,
    SkRect clip,
    const SkTextStyle& style,
    SkPath& path) const {

  paint.setStyle(SkPaint::kStroke_Style);
  if (style.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(style.getColor());
  } else {
    paint.setColor(style.getDecorationColor());
  }

  SkScalar scaleFactor = style.getFontSize() / 14.f;

  switch (style.getDecorationStyle()) {
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

SkScalar SkLine::paintDecorations(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkScalar offsetX) const {

  if (style.getDecoration() == SkTextDecoration::kNoDecoration) {
    // Still need to calculate text advance
    return iterateThroughRuns(text, offsetX,
                              [](SkRun* run, int32_t, size_t, SkRect, SkScalar) { return true; });
  }

  return this->iterateThroughRuns(
      text, offsetX,
      [this, canvas, style](SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift) {

        SkScalar thickness = style.getDecorationThicknessMultiplier();
        SkScalar position;
        switch (style.getDecoration()) {
          case SkTextDecoration::kUnderline:
            position = - run->ascent() + thickness;
            break;
          case SkTextDecoration::kOverline:
            position = 0;
            break;
          case SkTextDecoration::kLineThrough: {
            position = (run->descent() - run->ascent() - thickness) / 2;
            break;
          }
          default:
            position = 0;
            SkASSERT(false);
            break;
        }

        auto width = clip.width();
        SkScalar x = clip.left();
        SkScalar y = clip.top() + position;

        // Decoration paint (for now) and/or path
        SkPaint paint;
        SkPath path;
        this->computeDecorationPaint(paint, clip, style, path);
        paint.setStrokeWidth(thickness);

        switch (style.getDecorationStyle()) {
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
        return true;
      });
}