/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "uchar.h"
#include "SkColor.h"
#include "SkCanvas.h"
#include "SkFontMetrics.h"
#include "SkShaper.h"
#include "SkSpan.h"
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"
#include "SkTextBlobPriv.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "SkShapedRun.h"
#include "SkShapedLine.h"

// Comes from the paragraph
struct StyledText {

    StyledText(SkSpan<const char> text, SkTextStyle style)
        : fText(text), fStyle(style) {}

    bool operator==(const StyledText& rhs) const {
        return fText.begin() == rhs.fText.begin() &&
            fText.end() == rhs.fText.end() && // TODO: Can we have == on SkSpan?
            fStyle == rhs.fStyle;
    }
    SkSpan<const char> fText;
    SkTextStyle fStyle;
};

class SkShapedParagraph final : SkShaper::RunHandler {
  public:

    SkShapedParagraph(SkParagraphStyle style, std::vector<StyledText> styles);

    void layout(SkScalar maxWidth, size_t maxLines);

    void format(SkScalar maxWidth);

    void paint(SkCanvas* textCanvas, SkPoint& point);

    SkScalar alphabeticBaseline() { return fAlphabeticBaseline; }
    SkScalar height() { return fHeight; }
    SkScalar width() { return fWidth; }
    SkScalar ideographicBaseline() { return fIdeographicBaseline; }
    SkScalar maxIntrinsicWidth() { return fMaxIntrinsicWidth; }
    SkScalar minIntrinsicWidth() { return fMinIntrinsicWidth; }

    void GetRectsForRange(const char* start,
                          const char* end,
                          std::vector<SkTextBox>& result);

    size_t lineNumber() const { return fLines.size(); }

  private:

    // SkShaper::RunHandler interface
    SkShaper::RunHandler::Buffer newRunBuffer(const RunInfo& info,
                                              const SkFont& font,
                                              int glyphCount,
                                              SkSpan<const char> utf8) override {
        auto& word = fLines.back().addWord(font, info, glyphCount, utf8);
        return word.newRunBuffer();
    }

    void commitRun(SkScalar width) override {
        auto& line = fLines.back();   // Last line
        auto& word = line.lastWord(); // Last word

        // Finish the word
        word.finish(line.advance(), width);

        // Update the line stats
        line.update();

        // Update the paragraph stats
        fMaxIntrinsicWidth = SkMaxScalar(fMaxIntrinsicWidth, line.advance().fX);
        fMinIntrinsicWidth = SkMaxScalar(fMinIntrinsicWidth, word.advance().fX);
    }

    void commitLine() override {
        // Finish the line
        auto& line = fLines.back();
        line.finish();

        // Update the paragraph stats
        fHeight += line.advance().fY;
        fWidth = SkMaxScalar(fWidth, line.advance().fX);

        // Add the next line
        fLines.emplace_back();
    }

    // For debugging
    void printBlocks(size_t linenum);

    // Constrains
    size_t _maxLines;

    // Input
    SkParagraphStyle fParagraphStyle;
    std::vector<StyledText> fTextStyles;

    // Output to Flutter
    SkScalar fAlphabeticBaseline;   // TODO: Not implemented yet
    SkScalar fIdeographicBaseline;  // TODO: Not implemented yet
    SkScalar fHeight;
    SkScalar fWidth;
    SkScalar fMaxIntrinsicWidth;
    SkScalar fMinIntrinsicWidth;

    // Internal structures
    bool _exceededLimits;           // TODO: Ellipses not implemented yet
    SkTArray<SkShapedLine> fLines;  // All lines that the shaper produced
};