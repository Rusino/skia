/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkLine.h"

SkLine::SkLine() {
    fShift = 0;
    fAdvance.set(0, 0);
    fOffset.set(0, 0);
    fWidth = 0;
    fHeight = 0;
}

void SkLine::formatByWords(SkTextAlign effectiveAlign, SkScalar maxWidth) {
    SkScalar delta = maxWidth - advance().fX;
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
            if (fWords.size() == 1) {
                break;
            }
            SkScalar step = delta / (fWords.size() - 1);
            SkScalar shift = 0;
            for (auto& word : fWords) {
                word.shift(shift);
                if (&word == fWords.end()) {
                    break;
                }
                word.expand(step);
                shift += step;
            }
            fShift = 0;
            fAdvance.fX = maxWidth;
            fWidth = maxWidth;
            break;
        }
        default:break;
    }
}

void SkLine::paintByStyles(SkCanvas* canvas) {

    // TODO: For now we paint everything by words but we better combine words by style
    for (auto word : fWords) {

        word.paint(canvas);
    }
}

void SkLine::getRectsForRange(
    SkTextDirection textDirection,
    const char* start,
    const char* end,
    std::vector<SkTextBox>& result) {

    for (auto& word : fWords) {
        if (word.text().end() <= start || word.text().begin() >= end) {
            continue;
        }
        result.emplace_back(word.rect(), textDirection);
    }
}
