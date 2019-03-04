/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"

struct Block {
    Block(size_t start, size_t end, SkTextStyle style)
        : fStart(start), fEnd(end), fStyle(style) {}
    size_t fStart;
    size_t fEnd;
    SkTextStyle fStyle;
};

class SkCanvas;
class SkSection;
class SkPicture;

class SkParagraph {
  public:
    SkParagraph(
        const std::u16string& utf16text,
        SkParagraphStyle style,
        std::vector<Block> blocks);

    SkParagraph(
        const std::string& utf8text,
        SkParagraphStyle style,
        std::vector<Block> blocks);

    ~SkParagraph();

    double getMaxWidth() { return SkScalarToDouble(fWidth); }

    double getHeight() { return SkScalarToDouble(fHeight); }

    double getMinIntrinsicWidth() { return SkScalarToDouble(fMinIntrinsicWidth); }

    double getMaxIntrinsicWidth() { return SkScalarToDouble(fMaxIntrinsicWidth); }

    double getAlphabeticBaseline() { return SkScalarToDouble(fAlphabeticBaseline); }

    double getIdeographicBaseline() { return SkScalarToDouble(fIdeographicBaseline); }

    bool didExceedMaxLines() {

        return !fParagraphStyle.unlimited_lines()
            && fLinesNumber > fParagraphStyle.getMaxLines();
    }

    bool layout(double width);

    void paint(SkCanvas* canvas, double x, double y);

    std::vector<SkTextBox> getRectsForRange(
        unsigned start,
        unsigned end,
        RectHeightStyle rectHeightStyle,
        RectWidthStyle rectWidthStyle);

    SkPositionWithAffinity getGlyphPositionAtCoordinate(double dx, double dy) const;

    SkRange<size_t> getWordBoundary(unsigned offset);

  private:

    friend class ParagraphTester;

    // Record a picture drawing all small text blobs
    void recordPicture();

    // Break the text by explicit line breaks
    void breakTextIntoSections();

    // Things for Flutter
    SkScalar fAlphabeticBaseline;
    SkScalar fIdeographicBaseline;
    SkScalar fHeight;
    SkScalar fWidth;
    SkScalar fMaxIntrinsicWidth;
    SkScalar fMinIntrinsicWidth;
    SkScalar fMaxLineWidth;
    size_t fLinesNumber;

    // Input
    SkParagraphStyle fParagraphStyle;
    std::vector<StyledText> fTextStyles;
    SkSpan<const char> fUtf8;

    // Shaping (list of sections separated by hard line breaks)
    SkTArray<std::unique_ptr<SkSection>> fSections;

    // Painting
    sk_sp<SkPicture> fPicture;
};