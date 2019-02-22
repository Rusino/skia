/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkWord.h"
#include "SkRun.h"

SkWord::SkWord(SkSpan<const char> text, SkRun* begin, SkRun* end) // including begin and end
    : fText(text) {

    // Clusters
    auto cStart = SkTMax(text.begin(), begin->fText.begin()) - begin->fText.begin();
    auto cEnd = SkTMin(text.end(), end->fText.end()) - end->fText.begin();

    auto size = size_t(0);
    // Find the starting glyph position for the first run
    auto gStart = size_t(0);
    if (text.begin() < begin->fText.begin()) {
        while (gStart < begin->size() && begin->fClusters[gStart] < cStart) {
            ++gStart;
        }
    }
    size += begin->size() - gStart;

    for (auto& iter = begin; iter != end; ++iter) {
        if (iter != begin && iter != end) {
            size += iter->size();
        }
    }

    // find the ending glyph position for the last run
    auto gEnd = end->size();
    if (text.end() > end->fText.end()) {
        while (gEnd > gStart && end->fClusters[gEnd - 1] > cEnd) {
            --gEnd;
        }
    }
    size += end->size() - gEnd;

    SkTextBlobBuilder builder;

    for (auto& iter = begin; iter != end; ++iter) {
        auto gStart = size_t(0);
        auto gEnd = iter->size();
        if (iter == begin) {
            while (gStart < begin->size() && begin->fClusters[gStart] < cStart) {
                ++gStart;
            }
        } else if (iter == end) {
            while (gEnd > gStart && end->fClusters[gEnd - 1] > cEnd) {
                --gEnd;
            }
        }

        const auto& blobBuffer = builder.allocRunPos(iter->fFont, gEnd - gStart);
        sk_careful_memcpy(blobBuffer.glyphs,
                          iter->fGlyphs.data() + gStart,
                          (gEnd - gStart) * sizeof(SkGlyphID));

        for (size_t i = gStart; i < gEnd; ++i) {
            blobBuffer.points()[i - gStart] = iter->fPositions[SkToInt(i)];
        }
    }

    fBlob = builder.make();
}

SkWord::SkWord(SkSpan<const char> text, SkRun& run)
    : fText(text) {

    SkTextBlobBuilder builder;
    const auto& blobBuffer = builder.allocRunPos(run.fFont, run.size());

    sk_careful_memcpy(blobBuffer.glyphs, run.fGlyphs.data(), run.size() * sizeof(SkGlyphID));
    sk_careful_memcpy(blobBuffer.pos, run.fPositions.data(), run.size() * sizeof(SkPoint));

    fBlob = builder.make();
}

void SkWord::paint(SkCanvas* canvas) {

    // Do it somewhere else much earlier
    auto start = fStyles.begin();
    // Find the first style that affects the run
    while (start != fStyles.end()
        && start->fText.begin() < fText.begin()) {
        ++start;
    }

    auto end = start;
    while (end != fStyles.end()
        && end->fText.begin() < fText.end()) {
        ++end;
    }
    // TODO: move ShapedRun functionality here
    /*
    for (auto iter = fStyles.begin(); iter != fStyles.end(); ++iter) {

        auto style = iter->fStyle;
        auto part = this->applyStyle(iter->fText, iter->fStyle);

        part->paintBackground(canvas);
        part->paintShadow(canvas);

        SkPaint paint;
        if (style.hasForeground()) {
            paint = style.getForeground();
        } else {
            paint.reset();
            paint.setColor(style.getColor());
        }
        paint.setAntiAlias(true);
        canvas->drawTextBlob(part->fBlob, part->fShift, 0, paint);

        part->paintDecorations(canvas);
    }
     */
}