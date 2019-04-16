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
  this->fShift = other.fShift;
  this->fAdvance = other.fAdvance;
  this->fWidth = other.fWidth;
  this->fOffset = other.fOffset;
  this->fEllipsis = other.fEllipsis;
  this->fSizes = other.sizes();
  this->fReindexing.reset();
  other.fReindexing.foreach([this](const char* ch, SkCluster* cluster) {
    this->fReindexing.set(ch, cluster);
  });
}

void SkLine::breakLineByWords(UBreakIteratorType type, std::function<void(SkWord& word)> apply) {

  // TODO: do not create more breakers that we have to
  SkTextBreaker breaker;
  if (!breaker.initialize(fText, type)) {
    return;
  }
  fWords.reset();
  size_t currentPos = 0;
  while (true) {
    auto start = currentPos;
    currentPos = breaker.next(currentPos);
    if (breaker.eof()) {
      break;
    }
    SkSpan<const char> text(fText.begin() + start, currentPos - start);
    fWords.emplace_back(text);
    apply(fWords.back());
  }
}

void SkLine::reshuffle(SkCluster* start, SkCluster* end) {

  SkASSERT(start->fRun->index() <= end->fRun->index());
  size_t numRuns = end->fRun->index() - start->fRun->index() + 1;

  // Get the logical order
  std::vector<UBiDiLevel> runLevels;
  for (auto run = start->fRun; run <= end->fRun; ++run) {
    runLevels.emplace_back(run->fBidiLevel);
  }
  std::vector<int32_t> logicalFromVisual(numRuns);
  ubidi_reorderVisual(runLevels.data(), SkToU32(numRuns), logicalFromVisual.data());

  // Build the reindex table
  SkTArray<size_t> shifts(SkToU32(numRuns));
  size_t shift = 0;
  for (auto& logical : logicalFromVisual) {
    shifts.push_back(shift);
    SkDebugf("shift %d -> %d\n", start->fRun->index() + shifts.size() - 1, shift);
    auto run = start->fRun + logical;
    shift += &logical == &logicalFromVisual.front() ? run->size() - start->fStart : run->size();
  }

  SkTHashMap<const char*, size_t> perLine;
  for (auto cluster = start; cluster <= end; ) {

    // Move all the clusters for the run
    size_t runIndex = cluster->fRun->index();
    auto starting = shifts[runIndex - start->fRun->index()];
    size_t inside = 0;
    while (cluster->fRun->index() == runIndex) {
      auto newCluster = start + starting + inside;
      fReindexing.set(cluster->fText.begin(), newCluster);
      SkDebugf("Reindex '%s': @%d", toString(cluster->fText).c_str(), newCluster->fRun->index());
      if (newCluster->fStart + 1 == newCluster->fEnd) {
        SkDebugf("[%d]\n", newCluster->fStart);
      } else {
        SkDebugf("[%d:%d]\n", newCluster->fStart, newCluster->fEnd - 1);
      }
      ++inside;
      ++cluster;
      if (cluster > end) {
        break;
      }
    }
  }
}

SkCluster* SkLine::findCluster(const char* ch) const {

  const char* start = ch;

  while (start >= fText.begin()) {
    auto found = fReindexing.find(start);
    if (found != nullptr) {
      auto cluster = *found;
      SkASSERT(cluster->fText.begin() <= ch && cluster->fText.end() > ch);
      return cluster;
    }
    --start;
  }
  return nullptr;
}

SkVector SkLine::measureText(SkSpan<const char> text) const {

  SkVector size = SkVector::Make(0, 0);
  if (text.empty()) {
    return size;
  }

  auto start = findCluster(text.begin());
  auto end = findCluster(text.end() - 1);
  for (auto cluster = start; cluster <= end; ++cluster) {

    if (cluster == start) {
      size.fX -= cluster->sizeToChar(text.begin());
    }
    if (cluster == end) {
      size.fX += cluster->sizeFromChar(text.end() - 1);
    } else {
      size.fX += cluster->fWidth;
    }
    size.fY = SkTMax(size.fY, cluster->fHeight);
  }

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
                             [shift](SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar) {
                               for (auto i = pos; i < pos + size; ++i) {
                                 run->fPositions[i].fX += shift;
                               }
                               return true;
                             });
  }

  this->fShift = 0;
  this->fAdvance.fX = maxWidth;
  this->fWidth = maxWidth;
}


SkScalar SkLine::iterateThroughRuns(
    SkSpan<const char> text,
    SkScalar runOffset,
    std::function<void(SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift)> apply) const {

  if (text.empty()) {
    return 0;
  }

  // Find the starting and the ending cluster
  SkCluster* start = nullptr;
  for (auto ch = text.begin(); ch >= this->fText.begin(); --ch) {
    auto found = fReindexing.find(ch);
    if (found != nullptr) {
      // The character can be in the middle of a cluster; find the next and then go back
      start = *found;
      break;
    }
  }

  SkCluster* end = nullptr;
  for (auto ch = text.end() - 1; ch >= this->fText.begin(); --ch) {
    auto found = fReindexing.find(ch);
    if (found != nullptr) {
      // The character can be in the middle of a cluster; the previous cluster is always good
      end = *found;
      break;
    }
  }
  SkASSERT(start != nullptr && end != nullptr);

  SkScalar width = 0;
  for (auto current = start; current <= end + 1; ++current) {
    if (current->fRun == start->fRun && current <= end) {
      continue;
    }

    auto finish = current == end + 1 ? end->fText.end() : current->fText.end();
    SkASSERT(finish >= start->fText.end());
    SkSpan<const char> runText(start->fText.begin(), finish - start->fText.begin());
    SkDebugf("runText: '%s'\n", toString(runText).c_str());
    SkSpan<const char> intersect = text * runText;
    SkDebugf("intersect: '%s'\n", toString(intersect).c_str());

    // Find a part of the run that intersects with the text
    auto run = start->fRun;
    if (!run->leftToRight()) {
      std::swap(start, end);
    }

    auto lineOffset = run->position(0).fX;
    size_t size = 0;
    size_t pos = start->fStart;
    SkRect clip = SkRect::MakeXYWH( 0,
                                    run->sizes().diff(sizes()),
                                    0,
                                    run->calculateHeight());
    clip.offset(run->offset());
    SkScalar leftGlyphDiff = 0;
    for (auto cluster = start; cluster < current; ++cluster) {

      size += (cluster->fEnd - cluster->fStart);
      if (cluster == start) {
        clip.fLeft = cluster->fRun->position(cluster->fStart).fX - lineOffset;
        clip.fRight = clip.fLeft;
        clip.fLeft += cluster->sizeToChar(intersect.begin());
      }
      if (cluster == current) {
        clip.fRight += cluster->sizeFromChar(intersect.end() - 1);
      } else {
        //clip.fRight += cluster->fWidth; (because of justification)
        clip.fRight += cluster->fRun->calculateWidth(cluster->fStart, cluster->fEnd);
      }
    }

    auto shift1 = runOffset - clip.fLeft;
    auto shift2 = runOffset - clip.fLeft - lineOffset;
    clip.offset(shift1, 0);
    if (leftGlyphDiff != 0) {
      clip.fLeft += leftGlyphDiff;
    }
    SkDebugf("%f -%f - %f = %f (%f)\n", runOffset, clip.fLeft, lineOffset, shift2, clip.width());
    apply(run, pos, size, clip, shift2);

    width += clip.width();
    runOffset += clip.width();

    // TODO: calculate the ellipse for the last visual run
    if (this->ellipsis() != nullptr) {
      auto ellipsis = this->ellipsis();
      apply(ellipsis, 0, ellipsis->size(), ellipsis->clip(), ellipsis->offset().fX);
    }

    start = current;
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
    SkDebugf("!!!\n");
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