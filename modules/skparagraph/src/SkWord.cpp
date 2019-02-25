/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkWord.h"
#include "SkRun.h"

SkWord::SkWord(SkSpan<const char> text, SkSpan<SkRun> runs)
: fText(text)
, fShift(0)
, fRuns(runs)
, fBlob(nullptr) {
    // begin and end runs intersect with the word

    // Clusters
    auto cStart = SkTMax(fText.begin(), fRuns.begin()->fText.begin()) - fRuns.begin()->fText.begin();
    auto cEnd = SkTMin(fText.end(), fRuns.end()->fText.end()) - fRuns.end()->fText.begin();

    // Find the starting glyph position for the first run
    fLeft = 0;
    if (fText.begin() > fRuns.begin()->fText.begin()) {
        while (fLeft < fRuns.begin()->fText.size() && fRuns.begin()->fClusters[fLeft] < cStart) {
            ++fLeft;
        }
    }


    // find the ending glyph position for the last run
    fRight = fRuns.end()->fText.size();
    if (fText.end() < fRuns.end()->fText.end()) {
        while (fRight > fLeft && fRuns.end()->fClusters[fRight - 1] >= cEnd) {
            --fRight;
        }
    }

    fOffset = fRuns.begin()->offset();
    fAdvance = SkVector::Make(0, 0);
    auto iter = fRuns.begin();
    do {
        auto gStart = iter == fRuns.begin() ? fLeft : 0;
        auto gEnd = iter == fRuns.end() ? fRight : iter->size();
        if (gStart >= gEnd) {
            if (fAdvance.fX == 0) {
                continue;
            } else {
                break;
            }
        }

        if (iter != fRuns.begin() && iter != fRuns.end()) {
            fAdvance += iter->advance();
        } else {
            if (iter == fRuns.begin()) {
                fOffset += getOffset(*iter, gStart);
            }
            fAdvance += getAdvance(*iter, gStart, gEnd);
        }

        ++iter;
    } while(iter != fRuns.end());
}

void SkWord::generate(SkVector offset) {
    fOffset -= offset;
    SkDebugf("generate word: @%f:%f +%f:%f [%d:%d]\n", fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY, fLeft, fRight);

    SkTextBlobBuilder builder;
    auto iter = fRuns.begin();
    do {
        auto gStart = iter == fRuns.begin() ? fLeft : 0;
        auto gEnd = iter == fRuns.end() ? fRight : iter->size();
        if (gStart >= gEnd) {
            if (fAdvance.fX == 0) {
                continue;
            } else {
                break;
            }
        }

        const auto& blobBuffer = builder.allocRunPos(iter->fFont, gEnd - gStart);
        sk_careful_memcpy(blobBuffer.glyphs,
                          iter->fGlyphs.data() + gStart,
                          (gEnd - gStart) * sizeof(SkGlyphID));

        for (size_t i = gStart; i < gEnd; ++i) {
            blobBuffer.points()[i - gStart] = iter->fPositions[SkToInt(i)] - offset;
        }

        ++iter;
    } while(iter != fRuns.end());

    fBlob = builder.make();
}

SkVector SkWord::getAdvance(const SkRun& run, size_t start, size_t end) {
    //return (end == run.size() ? run.advance() : run.fPositions[end - 1]) -
    //                SkVector::Make(run.fPositions[start].fX, 0);
    return SkVector::Make((end == run.size()
                            ? run.advance().fX
                            : run.fPositions[end - 1].fX) - run.fPositions[start].fX,
                           run.advance().fY);
}

SkVector SkWord::getOffset(const SkRun& run, size_t start) {
    return SkVector(start == 0 ? run.offset() : run.offset() + run.fPositions[start]);

}

// TODO: Optimize painting by colors (paints)
void SkWord::paint(SkCanvas* canvas, SkPoint point, SkSpan<StyledText> styles) {

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

    fStyles = SkSpan<StyledText>(start, end - start);
    // TODO: deal with different styles
    paintBackground(canvas, point);
    paintShadow(canvas);

    auto style = this->fStyles.begin()->fStyle;
    SkPaint paint;
    if (style.hasForeground()) {
        paint = style.getForeground();
    } else {
        paint.reset();
        paint.setColor(style.getColor());
    }
    paint.setAntiAlias(true);
    canvas->drawTextBlob(fBlob, point.fX + fShift, point.fY, paint);

    paintDecorations(canvas);
}

void SkWord::paintShadow(SkCanvas* canvas) {

    auto fStyle = this->fStyles.begin()->fStyle;
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

void SkWord::paintBackground(SkCanvas* canvas, SkPoint point) {

    SkRect fRect = SkRect::MakeXYWH(point.fX + fOffset.fX, point.fY, fAdvance.fX, fAdvance.fY);
    auto fStyle = this->fStyles.begin()->fStyle;
    if (!fStyle.hasBackground()) {
        return;
    }

    fRect.offset(fShift, 0);
    canvas->drawRect(fRect, fStyle.getBackground());
}

SkScalar SkWord::computeDecorationThickness(SkTextStyle textStyle) {

    SkScalar thickness = 1.0f;

    SkFontMetrics metrics;
    textStyle.getFontMetrics(&metrics);

    switch (textStyle.getDecoration()) {
        case SkTextDecoration::kUnderline:
            if (!metrics.hasUnderlineThickness(&thickness)) {
                thickness = 1.f;
            }
            break;
        case SkTextDecoration::kOverline:break;
        case SkTextDecoration::kLineThrough:
            if (!metrics.hasStrikeoutThickness(&thickness)) {
                thickness = 1.f;
            }
            break;
        default:SkASSERT(false);
    }

    thickness = SkMaxScalar(thickness, textStyle.getFontSize() / 14.f);

    return thickness * textStyle.getDecorationThicknessMultiplier();
}

SkScalar SkWord::computeDecorationPosition(SkScalar thickness) {

    SkRect fRect = SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY);
    auto fStyle = this->fStyles.begin()->fStyle;
    SkFontMetrics metrics;
    fStyle.getFontMetrics(&metrics);

    SkScalar position;

    switch (fStyle.getDecoration()) {
        case SkTextDecoration::kUnderline:
            if (metrics.hasUnderlinePosition(&position)) {
                return position - metrics.fAscent;
            } else {
                position = metrics.fDescent - metrics.fAscent;
                if (fStyle.getDecorationStyle()
                    == SkTextDecorationStyle::kWavy ||
                    fStyle.getDecorationStyle()
                        == SkTextDecorationStyle::kDouble
                    ) {
                    return position - thickness * 3;
                } else {
                    return position - thickness;
                }
            }

            break;
        case SkTextDecoration::kOverline:return 0;
            break;
        case SkTextDecoration::kLineThrough: {
            SkScalar delta = fRect.height()
                - (metrics.fDescent - metrics.fAscent + metrics.fLeading);
            position =
                SkTMax(0.0f, delta) + (metrics.fDescent - metrics.fAscent) / 2;
            break;
        }
        default:position = 0;
            SkASSERT(false);
            break;
    }

    return position;
}

void SkWord::computeDecorationPaint(SkPaint& paint, SkPath& path) {

    SkRect fRect = SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY);
    auto fStyle = this->fStyles.begin()->fStyle;
    paint.setStyle(SkPaint::kStroke_Style);
    if (fStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
        paint.setColor(fStyle.getColor());
    } else {
        paint.setColor(fStyle.getDecorationColor());
    }
    paint.setAntiAlias(true);

    SkScalar scaleFactor = fStyle.getFontSize() / 14.f;

    switch (fStyle.getDecorationStyle()) {
        case SkTextDecorationStyle::kSolid:break;

        case SkTextDecorationStyle::kDouble:break;

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

void SkWord::paintDecorations(SkCanvas* canvas) {

    SkRect fRect = SkRect::MakeXYWH(fOffset.fX, fOffset.fY, fAdvance.fX, fAdvance.fY);
    auto fStyle = this->fStyles.begin()->fStyle;
    if (fStyle.getDecoration() == SkTextDecoration::kNone) {
        return;
    }

// Decoration thickness
    SkScalar thickness = computeDecorationThickness(fStyle);

// Decoration position
    SkScalar position = computeDecorationPosition(thickness);

// Decoration paint (for now) and/or path
    SkPaint paint;
    SkPath path;
    this->computeDecorationPaint(paint, path);
    paint.setStrokeWidth(thickness);

// Draw the decoration
    auto width = fRect.width();
    SkScalar x = fRect.left() + fShift;
    SkScalar y = fRect.top() + position;
    switch (fStyle.getDecorationStyle()) {
        case SkTextDecorationStyle::kWavy:path.offset(x, y);
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
        default:break;
    }
}
