/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkWord.h"
#include "SkRun.h"
#include <unicode/brkiter.h>
std::string toString1(SkSpan<const char> text) {
  icu::UnicodeString utf16 = icu::UnicodeString(text.begin(), text.size());
  std::string str;
  utf16.toUTF8String(str);
  return str;
}

SkWord::SkWord(SkSpan<const char> text, SkSpan<const char> spaces, bool lineBreakBefore)
    : fText(text)
    , fSpaces(spaces)
    , fShift(0)
    , fRuns()
    , fBlob(nullptr)
    , fTrimmed(false)
    , fMayLineBreakBefore(lineBreakBefore)
    , fProducedByShaper(false) {
}

SkWord::SkWord(SkSpan<const char> text, SkArraySpan<SkRun> runs)
    : fText(text)
    , fSpaces(SkSpan<const char>())
    , fShift(0)
    , fRuns()
    , fBlob(nullptr)
    , fTrimmed(false)
    , fMayLineBreakBefore(true)
    , fProducedByShaper(true) {
  mapToRuns(runs);
}

void SkWord::mapToRuns(SkArraySpan <SkRun> runs) {
  // begin and end runs intersect with the word
  fRuns = runs;
  auto first = fRuns.begin();
  auto last = fRuns.end() - 1;

  auto text = span();
  // Clusters
  auto cStart =
      SkTMax(text.begin(), first->fText.begin()) - first->fText.begin();
  auto cEnd = SkTMin(text.end(), last->fText.end()) - last->fText.begin();
  auto cTrim = SkTMin(fText.end(), last->fText.end()) - last->fText.begin();

  // Find the starting glyph position for the first run (in characters)
  gLeft = 0;
  if (text.begin() > first->fText.begin()) {
    while (gLeft < first->size() && SkToU32(first->fClusters[gLeft]) < SkToU32(cStart)) {
      ++gLeft;
    }
  }

  // find the ending glyph position for the last run (in characters)
  gRight = last->size();
  if (text.end() < last->fText.end()) {
    while (gRight > gLeft && SkToU32(last->fClusters[gRight - 1]) >= SkToU32(cEnd)) {
      --gRight;
    }
  }

  gTrim = gRight;
  if (!fSpaces.empty()) {
    while (gTrim > gLeft && SkToU32(last->fClusters[gTrim - 1]) >= SkToU32(cTrim)) {
      --gTrim;
    }
  }

  fOffset = SkVector::Make(0, 0);
  fHeight = 0;
  fFullWidth = 0;
  fRightTrimmedWidth = 0;
  fBaseline = 0;
  auto iter = first;
  do {
    auto gStart = iter == first ? gLeft : 0;
    auto gEnd = iter == last ? gRight : iter->size();
    if (gStart >= gEnd) {
      break;
    }

    SkVector advance;
    if (iter != first && iter != last) {
      advance = iter->advance();
    } else {
      advance = getAdvance(*iter, gStart, gEnd);
      if (iter == first) {
         fOffset.fX = iter->fPositions[gStart].fX;
      }
      auto trimmed = getAdvance(*iter, gStart, gTrim);
      fRightTrimmedWidth += trimmed.fX;
    }
    fHeight += advance.fY;
    fFullWidth += advance.fX;
    fBaseline = SkMaxScalar(fBaseline, - iter->fInfo.fAscent);

    ++iter;
  } while (iter <= last);

  SkASSERT(fFullWidth >= 0);
  SkASSERT(fRightTrimmedWidth >= 0);
  SkDebugf("Word%s '%s' %f ~ %f\n",
           fMayLineBreakBefore ? " " : "+",
           toString1(fText).c_str(),
           fFullWidth,
           fOffset.fX);
}

void SkWord::generate(SkVector offset) {
  // begin and end runs intersect with the word
  auto first = fRuns.begin();
  auto last = fRuns.end() - 1;

  fOffset -= offset;
  SkTextBlobBuilder builder;
  auto iter = fRuns.begin();
  do {
    auto gStart = iter == first ? gLeft : 0;
    auto gEnd = iter == last ? fTrimmed ? gTrim : gRight : iter->size();
    if (gStart >= gEnd) {
      break;
    }

    const auto& blobBuffer = builder.allocRunPos(iter->fFont, gEnd - gStart);
    sk_careful_memcpy(blobBuffer.glyphs,
                      iter->fGlyphs.data() + gStart,
                      (gEnd - gStart) * sizeof(SkGlyphID));

    for (size_t i = gStart; i < gEnd; ++i) {
      SkVector point = iter->fPositions[SkToInt(i)] - offset;
      point.fY = - first->fInfo.fAscent;
      blobBuffer.points()[i - gStart] = point;
    }

    ++iter;
  } while (iter <= last);

  fBlob = builder.make();
}

SkVector SkWord::getAdvance(const SkRun& run, size_t start, size_t end) {
  return SkVector::Make((end == run.size()
                         ? run.advance().fX - (run.fPositions[start].fX - run.fPositions[0].fX)
                         : run.fPositions[end].fX - run.fPositions[start].fX),
                        run.advance().fY);
}

// TODO: Optimize painting by colors (paints)
void SkWord::paint(SkCanvas* canvas) {

  // TODO: deal with different styles
  auto style = this->fTextStyles.begin()->fStyle;
  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.reset();
    paint.setColor(style.getColor());
  }
  paint.setAntiAlias(true);
  canvas->drawTextBlob(fBlob, fShift, 0, paint);
}

void SkWord::dealWithStyles(SkSpan<StyledText> styles) {
  // Do it somewhere else much earlier
  auto start = styles.begin();
  // Find the first style that affects the run
  while (start != styles.end()
      && start->fText.end() <= fText.begin()) {
    ++start;
  }

  auto end = start;
  while (end != styles.end()
      && end->fText.begin() < fText.end()) {
    ++end;
  }

  fTextStyles = SkSpan<StyledText>(start, end - start);
}

void SkWord::paintShadow(SkCanvas* canvas) {

  auto fStyle = this->fTextStyles.begin()->fStyle;
  if (fStyle.getShadowNumber() == 0) {
    return;
  }

  for (SkTextShadow shadow : fStyle.getShadows()) {
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
    canvas->drawTextBlob(fBlob,
                         fShift + shadow.fOffset.x(),
                         shadow.fOffset.y(),
                         paint);
  }
}

void SkWord::paintBackground(SkCanvas* canvas) {

  SkRect rect = SkRect::MakeXYWH(fShift + fOffset.fX, fOffset.fY, fFullWidth, fHeight);
  auto fStyle = this->fTextStyles.begin()->fStyle;
  if (!fStyle.hasBackground()) {
    return;
  }

  rect.offset(fShift, 0);
  canvas->drawRect(rect, fStyle.getBackground());
}

void SkWord::computeDecorationPaint(SkPaint& paint, SkPath& path) {

  SkRect fRect = SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fFullWidth, fHeight);
  auto fStyle = this->fTextStyles.begin()->fStyle;
  paint.setStyle(SkPaint::kStroke_Style);
  if (fStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(fStyle.getColor());
  } else {
    paint.setColor(fStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);

  SkScalar scaleFactor = fStyle.getFontSize() / 14.f;

  switch (fStyle.getDecorationStyle()) {
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

      path.moveTo(0, 0);
      auto width = fRect.width();
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

void SkWord::paintDecorations(SkCanvas* canvas, SkScalar baseline) {

  auto fStyle = this->fTextStyles.begin()->fStyle;
  if (fStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  // Decoration thickness
  SkScalar thickness = fStyle.getDecorationThicknessMultiplier() * fStyle.getFontSize() / 14.f;

  // Decoration position
  SkFontMetrics metrics;
  fStyle.getFontMetrics(&metrics);
  SkScalar position;
  switch (fStyle.getDecoration()) {
    case SkTextDecoration::kUnderline:
      position = baseline + thickness;
      break;
    case SkTextDecoration::kOverline:
      position = baseline + metrics.fAscent - thickness;
      break;
    case SkTextDecoration::kLineThrough: {
      position = baseline + metrics.fDescent + (metrics.fAscent - thickness) / 2;
      break;
    }
    default:
      position = 0;
      SkASSERT(false);
      break;
  }

// Decoration paint (for now) and/or path
  SkPaint paint;
  SkPath path;
  this->computeDecorationPaint(paint, path);
  paint.setStrokeWidth(thickness);

// Draw the decoration
  SkRect fRect = SkRect::MakeXYWH(fOffset.fX + fShift, fOffset.fY, fFullWidth, fHeight);
  auto width = fRect.width();
  SkScalar x = fRect.left() + fShift;
  SkScalar y = fRect.top() + position;
  switch (fStyle.getDecorationStyle()) {
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
}
