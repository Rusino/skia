/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkSection.h"
#include "SkFontMetrics.h"
#include "SkWord.h"

template<typename T>
inline bool operator == (const SkSpan<T> & a, const SkSpan<T> & b) {
    return a.size() == b.size() && a.begin() == b.begin();
}

template<typename T>
inline bool operator <= (const SkSpan<T> & a, const SkSpan<T> & b) {
    return a.begin() >= b.begin() && a.end() <= b.end();
}

template<typename T>
inline bool operator && (const SkSpan<T> & a, const SkSpan<T> & b) {
    return a.end() >= b.begin() && a.begin() <= b.end();
}

SkSection::SkSection(
    SkParagraphStyle style,
    std::vector<StyledText> styles,
    std::vector<SkSpan<const char>> softLineBreaks)
    : fParagraphStyle(style)
    , fTextStyles(std::move(styles))
    , fSoftLineBreaks(std::move(softLineBreaks)) {

    fAlphabeticBaseline = 0;
    fIdeographicBaseline = 0;
    fHeight = 0;
    fWidth = 0;
    fMaxIntrinsicWidth = 0;
    fMinIntrinsicWidth = 0;
    fStatus = ShapingNothing;
}

bool SkSection::shapeTextIntoEndlessLine() {

    if (!fTextStyles.empty()) return false;

    fMaxLines = 1;
    auto start = fTextStyles.begin()->fText.begin();
    auto end = fTextStyles.empty() ? start - 1 : std::prev(fTextStyles.end())->fText.end();
    if (start < end) return false;

    fStatus = ShapingOneLine;
    SkSpan<const char> run(start, end - start);
    MultipleFontRunIterator font(run, SkSpan<StyledText>(fTextStyles));
    SkShaper shaper(nullptr);
    shaper.shape(this,
                 &font,
                 start,
                 end - start,
                 true,
                 {0, 0},
                 std::numeric_limits<SkScalar>::max());

    fStatus = ShapingNothing;
    return true;
}

// This is the trickiest part: we need to break/merge shaper buffers
// Actually, the tricky part is hidden inside SkWord constructor
void SkSection::breakEndlessLineIntoWords() {

    auto wordIter = fSoftLineBreaks.begin();
    auto runIter = fRuns.begin();
    auto prevRunIter = runIter;
    while (wordIter != fSoftLineBreaks.end() && runIter != fRuns.end()) {
        auto wordSpan = *wordIter;
        auto runSpan = runIter->text();
        if (wordSpan == runSpan) {
            // One word, one run - ideal but only probable in short texts
            fWords.emplace_back(wordSpan, *runIter);
            ++wordIter;
            ++runIter;
        } else if (wordSpan <= runSpan) {
            // Few words in one run - normal case
            fWords.emplace_back(wordSpan, runIter, runIter);
            ++wordIter;
        } else if (wordSpan && runSpan) {
            // One words is spread between few runs - can happen unfortunatelly
            if (wordSpan.end() <= runSpan.end()) {
                // Copy all the runs affecting the word
                fWords.emplace_back(wordSpan, prevRunIter, runIter);
                // Move the iterator if we have to
                if (wordSpan.end() == runSpan.end()) {
                    ++runIter;
                }
            } else {
                // Continue with runs until we cover the entire word
                ++runIter;
                continue;
            }
        } else {
            // We are working with continuous sequences here
            SkASSERT(false);
        }
        prevRunIter = runIter;
    }
}

void SkSection::breakEndlessLineIntoLinesByWords(SkScalar width) {

    SkScalar lineWidth = 0;
    auto lineBegin = &fWords.front();
    for (auto& word : fWords) {
        if (lineWidth + word.advance().fX > width) {
            if (lineWidth == 0) {
                // The word is too big!
                // Re-shape this word with the given width and use all the breaks from the shaper
                shapeWordIntoManyLines(width, word);
                // The last line is not full
                auto last = fLines.back();
                lineWidth = last.advance().fX;
                fLines.pop_back();
            } else {
                // Add the line and start counting again
                fLines.emplace_back(lineWidth, SkSpan<SkWord>(lineBegin, &word - lineBegin));
                // Start the new line
                lineBegin = &word;
                lineWidth = 0;
            }
        } else {
            // Keep counting words
            lineWidth += word.advance().fX;
        }
    }

    // Do not forget the rest of the words
    fLines.emplace_back(lineWidth,SkSpan<SkWord>(lineBegin, fWords.end() - lineBegin));
}

void SkSection::shapeWordIntoManyLines(SkScalar width, SkWord& word) {

    auto start = word.fStyles.begin()->fText.begin();
    auto end = word.fStyles.empty() ? start - 1 : std::prev(word.fStyles.end())->fText.end();

    fStatus = ShapingOneWord;
    SkSpan<const char> run(start, end - start);
    MultipleFontRunIterator font(run, word.fStyles);
    SkShaper shaper(nullptr);
    fLines.emplace_back();
    shaper.shape(this,
                 &font,
                 start,
                 end - start,
                 true,
                 {0, 0},
                 // TODO: Can we be more specific with max line number?
                 std::numeric_limits<SkScalar>::max());
    fStatus = ShapingNothing;
}

void SkSection::shapeIntoLines(SkScalar maxWidth, size_t maxLines) {

    fMaxLines = maxLines;

    // Get rid of all the "empty text" cases
    if (fTextStyles.empty()) {
        // Shaper does not shape empty lines
        fHeight = 0;
        fWidth = 0;
        fMaxIntrinsicWidth = 0;
        fMinIntrinsicWidth = 0;
    } else {
        auto start = fTextStyles.begin()->fText.begin();
        auto end = fTextStyles.empty() ? start - 1 : std::prev(fTextStyles.end())->fText.end();
        if (start == end) {
            // Shaper does not shape empty lines
            SkFontMetrics metrics;
            fTextStyles.begin()->fStyle.getFontMetrics(metrics);
            fAlphabeticBaseline = -metrics.fAscent;
            fIdeographicBaseline = -metrics.fAscent;
            fHeight = metrics.fDescent + metrics.fLeading - metrics.fAscent;
            fWidth = 0;
            fMaxIntrinsicWidth = 0;
            fMinIntrinsicWidth = 0;
            return;
        }
    }

    shapeTextIntoEndlessLine();

    breakEndlessLineIntoWords();

    breakEndlessLineIntoLinesByWords(maxWidth);
}

void SkSection::formatLinesByWords(SkScalar maxWidth) {

    auto effectiveAlign = fParagraphStyle.effective_align();
    for (auto& line : fLines) {

        if (effectiveAlign == SkTextAlign::justify && &line == &fLines.back()) {
            effectiveAlign = SkTextAlign::left;
        }
        line.formatByWords(effectiveAlign, maxWidth);
    }
}

void SkSection::paintEachLineByStyles(SkCanvas* textCanvas) {

    for (auto& line : fLines) {
        line.paintByStyles(textCanvas);
    }
}

void SkSection::getRectsForRange(
    const char* start,
    const char* end,
    std::vector<SkTextBox>& result) {

    for (auto& line : fLines) {
        line.getRectsForRange(fParagraphStyle.getTextDirection(), start, end, result);
    }
}