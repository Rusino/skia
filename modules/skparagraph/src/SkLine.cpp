/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unicode/brkiter.h>
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

SkSpan<const char> operator*(const SkSpan<const char>& a, const SkSpan<const char>& b) {
  auto begin = SkTMax(a.begin(), b.begin());
  auto end = SkTMin(a.end(), b.end());
  return SkSpan<const char>(begin, end > begin ? end - begin : 0);
}

int32_t intersects(SkSpan<const char> a, SkSpan<const char> b) {
  if (a.begin() == nullptr || b.begin() == nullptr) {
    return -1;
  }
  auto begin = SkTMax(a.begin(), b.begin());
  auto end = SkTMin(a.end(), b.end());
  return end - begin;
}
}

SkTHashMap<SkFont, SkRun> SkLine::fEllipsisCache;

SkLine::SkLine(const SkLine& other) {
  this->fText = other.fText;
  this->fLogical.reset();
  this->fLogical = std::move(other.fLogical);
  this->fShift = other.fShift;
  this->fAdvance = other.fAdvance;
  this->fOffset = other.fOffset;
  this->fEllipsis.reset(other.fEllipsis == nullptr ? nullptr : new SkRun(*other.fEllipsis));
  this->fSizes = other.sizes();
  this->fClusters = other.fClusters;
}

// Paint parts of each style separately
void SkLine::paint(SkCanvas* textCanvas, SkSpan<SkBlock> blocks) {

  if (this->empty()) {
    return;
  }

  textCanvas->save();
  textCanvas->translate(this->offset().fX, this->offset().fY);

  this->iterateThroughStylesInTextOrder(
      SkStyleType::Background, blocks, true,
      [textCanvas, this]
      (SkSpan<const char> text, SkTextStyle style, SkScalar offsetX) {
        return this->paintBackground(textCanvas, text, style, offsetX);
      });

  this->iterateThroughStylesInTextOrder(
      SkStyleType::Shadow, blocks, true,
      [textCanvas, this]
      (SkSpan<const char> text, SkTextStyle style, SkScalar offsetX) {
        return this->paintShadow(textCanvas, text, style, offsetX);
      });

  this->iterateThroughStylesInTextOrder(
      SkStyleType::Foreground, blocks, true,
      [textCanvas, this]
      (SkSpan<const char> text, SkTextStyle style, SkScalar offsetX) {
        return this->paintText(textCanvas, text, style, offsetX);
      });

  this->iterateThroughStylesInTextOrder(
      SkStyleType::Decorations, blocks, true,
      [textCanvas, this]
      (SkSpan<const char> text, SkTextStyle style, SkScalar offsetX) {
        return this->paintDecorations(textCanvas, text, style, offsetX);
      });

  textCanvas->restore();
}

void SkLine::format(SkTextAlign effectiveAlign, SkScalar maxWidth, bool last) {
  SkScalar delta = maxWidth - this->width();
  if (delta <= 0) {
    // Delta can be < 0 if there are extra whitespaces at the end of the line;
    // This is a limitation of a current version
    return;
  }

  switch (effectiveAlign) {
    case SkTextAlign::left:

      this->shiftTo(0);
      break;
    case SkTextAlign::right:

      //this->setWidth(maxWidth);
      this->shiftTo(delta);
      break;
    case SkTextAlign::center: {

      //this->setWidth(maxWidth);
      this->shiftTo(delta / 2);
      break;
    }
    case SkTextAlign::justify: {

      if (last) {
        this->justify(maxWidth);
      } else {
        this->shiftTo(0);
      }

      break;
    }
    default:
      break;
  }
}

// Scan parts of each style separately
void SkLine::scanStyles(SkStyleType style, SkSpan<SkBlock> blocks,
                        std::function<void(SkTextStyle, SkScalar)> apply) {

  if (this->empty()) {
    return;
  }

  this->iterateThroughStylesInTextOrder(
      style, blocks, true,
      [this, apply](SkSpan<const char> text, SkTextStyle style, SkScalar offsetX) {
        apply(style, offsetX);
        return this->iterateThroughRuns(
            text, offsetX,
            [](SkRun*, int32_t, size_t, SkRect, SkScalar, bool) { return true; });
      });
}

void SkLine::scanRuns(std::function<void(SkRun*, int32_t, size_t, SkRect)> apply) {

  this->iterateThroughRuns(
      fText, 0,
      [apply](SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar, bool) {
        apply(run, pos, size, clip);
        return true;
      });
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

  auto shiftDown = this->baseline();
  return this->iterateThroughRuns(
    text, offsetX,
    [paint, canvas, shiftDown]
    (SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded) {

      SkTextBlobBuilder builder;
      run->copyTo(builder, SkToU32(pos), size, SkVector::Make(0, shiftDown));
      canvas->save();
      if (clippingNeeded) {
        canvas->clipRect(clip);
      }
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
    return iterateThroughRuns(
      text, offsetX,
      [](SkRun*, int32_t, size_t, SkRect, SkScalar, bool) { return true; });
  }
  return this->iterateThroughRuns(
    text, offsetX,
    [canvas, style](SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded) {
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
    return iterateThroughRuns(
        text, offsetX,
        [](SkRun*, int32_t, size_t, SkRect, SkScalar, bool) { return true; });
  }

  SkScalar result = 0;
  for (SkTextShadow shadow : style.getShadows()) {

    if (!shadow.hasShadow()) continue;

    SkPaint paint;
    paint.setColor(shadow.fColor);
    if (shadow.fBlurRadius != 0.0) {
      auto filter = SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, SkDoubleToScalar(shadow.fBlurRadius), false);
      paint.setMaskFilter(filter);
    }

    auto shiftDown = this->baseline();
    result = this->iterateThroughRuns(
      text, offsetX,
      [canvas, shadow, paint, shiftDown]
      (SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded) {
        SkTextBlobBuilder builder;
        run->copyTo(builder, pos, size, SkVector::Make(0, shiftDown));
        canvas->save();
        clip.offset(shadow.fOffset);
        if (clippingNeeded) {
          canvas->clipRect(clip);
        }
        canvas->translate(shift, 0);
        canvas->drawTextBlob(builder.make(), shadow.fOffset.x(), shadow.fOffset.y(), paint);
        canvas->restore();
        return true;
      });
  }

  return result;
}

SkScalar SkLine::paintDecorations(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkScalar offsetX) const {

  if (style.getDecoration() == SkTextDecoration::kNoDecoration) {
    // Still need to calculate text advance
    return iterateThroughRuns(
        text, offsetX,
        [](SkRun*, int32_t, size_t, SkRect, SkScalar, bool) { return true; });
  }

  return this->iterateThroughRuns(
    text, offsetX,
    [this, canvas, style]
    (SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded) {

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
          // TODO: a combination of several decorations
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
          canvas->drawLine(x, y, x + width, y, paint);
          break;
        default:
          break;
      }
      return true;
    });
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
      SkScalar wavelength = scaleFactor * style.getDecorationThicknessMultiplier();
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

void SkLine::reorderVisualRuns() {

  auto start = fClusters.begin();
  auto end = fClusters.end() - 1;
  size_t numRuns = end->run()->index() - start->run()->index() + 1;

  // Get the logical order
  std::vector<UBiDiLevel> runLevels;
  for (auto run = start->run(); run <= end->run(); ++run) {
    runLevels.emplace_back(run->fBidiLevel);
  }

  std::vector<int32_t> logicals(numRuns);
  ubidi_reorderVisual(runLevels.data(), SkToU32(numRuns), logicals.data());

  auto firstRun = start->run();
  for (auto logical : logicals) {
    fLogical.push_back(firstRun + logical);
  }
}

void SkLine::justify(SkScalar maxWidth) {

  size_t words = 0;
  SkScalar textLen = 0;
  bool wereWhitespaces = false;
  this->iterateThroughClustersInGlyphsOrder(false,
  [&words, &textLen, &wereWhitespaces](const SkCluster* cluster) {
    if (cluster->isWhitespaces()) {
      if (!wereWhitespaces) {
        ++words;
      }
      wereWhitespaces = true;
    } else {
      wereWhitespaces = false;
      textLen += cluster->width();
    }
    return true;
  });

  if (words == 0) {
    this->fShift = 0;
    //this->fAdvance.fX = maxWidth;
    return;
  }

  SkScalar step = (maxWidth - textLen) / words;
  SkScalar shift = 0;

  // Walk through the runs in the logical order
  wereWhitespaces = false;
  SkScalar whitespaceLen = 0;
  this->iterateThroughClustersInGlyphsOrder(false,
    [&shift, step, &wereWhitespaces, &words, &whitespaceLen](const SkCluster* cluster) {
      if (cluster->isWhitespaces()) {
        if (!wereWhitespaces) {
          // First whitespace (could be many in a row)
          wereWhitespaces = true;
          --words;
        }
        whitespaceLen += cluster->width();
      } else {
        // Shift the cluster
        if (wereWhitespaces) {
          // If it's the first character after the spaces, increase the shift
          shift += step;
        }
        for (auto j = cluster->startPos(); j != cluster->endPos(); ++j) {
          cluster->run()->fOffsets[j] = shift - whitespaceLen;
        }
        wereWhitespaces = false;
      }
      cluster->run()->fJustified = true;
      return true;
    });

  SkAssertResult(SkScalarNearlyEqual(shift, maxWidth - textLen));
  SkASSERT(words == 0);
  this->fShift = 0;
  this->fAdvance.fX = maxWidth;
}

void SkLine::createEllipsis(SkScalar maxWidth, const std::string& ellipsis, bool) {

  // Replace some clusters with the ellipsis
  // Go through the clusters in the reverse logical order
  // taking off cluster by cluster until the ellipsis fits
  SkScalar width = fAdvance.fX;
  iterateThroughClustersInGlyphsOrder(
    true,
    [this, &width, ellipsis, maxWidth](const SkCluster* cluster) {

      if (cluster->isWhitespaces()) {
        width -= cluster->width();
        return true;
      }

      // Shape the ellipsis
      SkRun* cached = fEllipsisCache.find(cluster->run()->font());
      if (cached == nullptr) {
        cached = shapeEllipsis(ellipsis, cluster->run());
      }

      fEllipsis = std::make_unique<SkRun>(*cached);

      // See if it fits
      if (width + fEllipsis->advance().fX > maxWidth) {
        width -= cluster->width();
        return true;
      }

      fEllipsis->shift(width, 0);
      fAdvance.fX = width; // + fEllipsis->fAdvance.fX;

      return false;
  });
}

SkRun* SkLine::shapeEllipsis(const std::string& ellipsis, SkRun* run) {

  class ShapeHandler final : public SkShaper::RunHandler {

   public:
    explicit ShapeHandler(SkScalar lineHeight) : fRun(nullptr), fLineHeight(lineHeight) { }
    SkRun* run() { return fRun; }

   private:

    void beginLine() override { }

    void runInfo(const RunInfo&) override { }

    void commitRunInfo() override { }

    Buffer runBuffer(const RunInfo& info) override {

      fRun = fEllipsisCache.set(info.fFont, SkRun(SkSpan<const char>(), info, fLineHeight, 0, 0));
      return fRun->newRunBuffer();
    }

    void commitRunBuffer(const RunInfo& info) override {
      fRun->fAdvance.fX = info.fAdvance.fX;
      fRun->fAdvance.fY = fRun->descent() - fRun->ascent();
    }

    void commitLine() override { }

    SkRun* fRun;
    SkScalar fLineHeight;
  };

  ShapeHandler handler(run->lineHeight());
  std::unique_ptr<SkShaper> shaper = SkShaper::MakeShapeThenWrap();
  shaper->shape(ellipsis.data(), ellipsis.size(),
                run->font(),
                true,
                std::numeric_limits<SkScalar>::max(),
                &handler);
  handler.run()->fText = SkSpan<const char>(ellipsis.data(), ellipsis.size());
  return handler.run();
}

SkVector SkLine::measureWordAcrossAllRuns(SkSpan<const char> word) const {

  SkVector wordSize = SkVector::Make(0, 0);
  for (auto run : fLogical) {

    // Find the intersection between the text and the run
    SkSpan<const char> intersect = run->text() * word;
    if (intersect.empty()) {
      continue;
    }

    size_t pos;
    size_t size;
    bool clippingNeeded;
    SkRect clip = this->measureTextInsideOneRun(intersect, run, pos, size, clippingNeeded);
    wordSize.fX += clip.width();
    wordSize.fY = SkTMax(wordSize.fY, clip.height());
  }
  return wordSize;
}

SkRect SkLine::measureTextInsideOneRun(SkSpan<const char> text,
                                       SkRun* run,
                                       size_t& pos,
                                       size_t& size,
                                       bool& clippingNeeded) const {

  SkASSERT(intersects(run->text(), text) >= 0);

  // Find [start:end] clusters for the text
  bool found;
  SkCluster* start;
  SkCluster* end;
  std::tie(found, start, end) = run->findClusters(text);
  SkASSERT(found);

  pos = start->startPos();
  size = end->endPos() - start->startPos();

  // Calculate the clipping rectangle for the text with cluster edges
  SkRect clip = SkRect::MakeXYWH( run->positionX(start->startPos()) - run->positionX(0),
                                  sizes().runTop(run),
                                  run->calculateWidth(start->startPos(), end->endPos()),
                                  run->calculateHeight());

  // Correct the width in case the text edges don't match clusters
  // TODO: This is where we get smart about selecting a part of a cluster
  //  by shaping each grapheme separately and then use the result sizes
  //  to calculate the proportions
  auto leftCorrection = start->sizeToChar(text.begin());
  auto rightCorrection = end->sizeFromChar(text.end() - 1);
  clip.fLeft  += leftCorrection;
  clip.fRight -= rightCorrection;
  clippingNeeded = leftCorrection != 0 || rightCorrection != 0;

  return clip;
}

void SkLine::iterateThroughClustersInGlyphsOrder(
    bool reverse, std::function<bool(const SkCluster* cluster)> apply) const {

  for (size_t r = 0; r != fLogical.size(); ++r) {
    auto& run = fLogical[reverse ? fLogical.size() - r - 1 : r];
    // Walk through the clusters in the logical order (or reverse)
    auto normalOrder = run->leftToRight() != reverse;
    auto start = normalOrder ? run->clusters().begin() : run->clusters().end() - 1;
    auto end = normalOrder ? run->clusters().end() : run->clusters().begin() - 1;
    for (auto cluster = start; cluster != end; normalOrder ? ++cluster : --cluster) {
      if (!this->contains(cluster)) {
        continue;
      }
      if (!apply(cluster)) {
        return;
      }
    }
  }
}

SkScalar SkLine::iterateThroughRuns(
    SkSpan<const char> text,
    SkScalar runOffset,
    std::function<bool(SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded)> apply) const {

  SkScalar width = 0;
  // Walk through the runs in the logical order
  for (auto& run : fLogical) {

    if (intersects(run->text(), text) <= 0) {
      continue;
    }
    // Find the intersection between the text and the run
    SkSpan<const char> intersect = run->text() * text;

    size_t pos;
    size_t size;
    bool clippingNeeded;
    SkRect clip = this->measureTextInsideOneRun(intersect, run, pos, size, clippingNeeded);

    auto shift = runOffset - clip.fLeft;
    clip.offset(shift, 0);
    if (clip.fRight > fAdvance.fX) {
      clip.fRight = fAdvance.fX;
      clippingNeeded = true; // Correct the clip in case there was an ellipsis
    } else if (run == fLogical.back() && this->ellipsis() != nullptr) {
      clippingNeeded = true; // To avoid trouble
    }
    if (!apply(run, pos, size, clip, shift - run->positionX(0), clippingNeeded)) {
      return width;
    }

    width += clip.width();
    runOffset += clip.width();
  }

  if (this->ellipsis() != nullptr) {
    auto ellipsis = this->ellipsis();
    if (!apply(ellipsis, 0, ellipsis->size(), ellipsis->clip(), ellipsis->clip().fLeft, false)) {
      return width;
    }
    width += ellipsis->clip().width();
  }

  return width;
}

void SkLine::iterateThroughStylesInTextOrder(
    SkStyleType styleType,
    SkSpan<SkBlock> blocks,
    bool checkOffsets,
    std::function<SkScalar(SkSpan<const char> text,
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

    auto width = apply(SkSpan<const char>(start, size), prevStyle, offsetX);
    offsetX += width;

    // Start all over again
    prevStyle = style;
    start = intersect.begin();
    size = intersect.size();
  }

  // The very last style
  auto width = apply(SkSpan<const char>(start, size), prevStyle, offsetX);
  offsetX += width;

  // This is not a trivial assert!
  // It asserts that 2 different ways of calculation come with the same results
  if (!SkScalarNearlyEqual(offsetX, this->width())) {
    SkDebugf("ASSERT: %f != %f '%s'\n", offsetX, this->width(), toString(fText).c_str());
  }
  if (checkOffsets) {
    SkASSERT(SkScalarNearlyEqual(offsetX, this->width()));
  }
}

