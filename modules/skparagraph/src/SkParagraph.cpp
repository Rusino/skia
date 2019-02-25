/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <unicode/brkiter.h>
#include "SkParagraph.h"
#include "SkPictureRecorder.h"

SkParagraph::SkParagraph(const std::string& text,
    SkParagraphStyle style,
    std::vector<Block> blocks)
    : fParagraphStyle(style), fUtf8(text.data(), text.size()), fPicture(nullptr) {

    std::transform(blocks.cbegin(),
                   blocks.cend(),
                   std::back_inserter(fTextStyles),
                   [this](const Block& value) {
                     return StyledText(SkSpan<const char>(
                         fUtf8.begin() + value.fStart,
                         value.fEnd - value.fStart), value.fStyle);
                   });
}

SkParagraph::SkParagraph(const std::u16string& utf16text,
    SkParagraphStyle style,
    std::vector<Block> blocks)
    : fParagraphStyle(style), fPicture(nullptr) {

    icu::UnicodeString unicode((UChar*) utf16text.data(), SkToS32(utf16text.size()));
    std::string str;
    unicode.toUTF8String(str);
    fUtf8 = SkSpan<const char>(str.data(), str.size());

    std::transform(blocks.cbegin(),
                   blocks.cend(),
                   std::back_inserter(fTextStyles),
                   [this](const Block& value) {
                     return StyledText(SkSpan<const char>(
                         fUtf8.begin() + value.fStart,
                         value.fEnd - value.fStart), value.fStyle);
                   });
}

SkParagraph::~SkParagraph() = default;

// TODO: optimize for intrinsic widths
bool SkParagraph::layout(double doubleWidth) {

    if (fSections.empty()) {
        // Break the text into sections separated by hard line breaks
        // (with each one broken into blocks by style)
        this->breakTextIntoSections();
    }

    // Collect Flutter values
    fAlphabeticBaseline = 0;
    fHeight = 0;
    fWidth = 0;
    fIdeographicBaseline = 0;
    fMaxIntrinsicWidth = 0;
    fMinIntrinsicWidth = 0;
    fLinesNumber = 0;
    fMaxLineWidth = 0;

    auto width = SkDoubleToScalar(doubleWidth);

    // Take care of line limitation across all the paragraphs
    size_t maxLines = fParagraphStyle.getMaxLines();
    for (auto& section : fSections) {

        // Shape
        section.shapeIntoLines(width, maxLines);

        // Make sure we haven't exceeded the limits
        fLinesNumber += section.lineNumber();
        if (!fParagraphStyle.unlimited_lines()) {
            maxLines -= section.lineNumber();
        }
        if (maxLines <= 0) {
            break;
        }

        fMaxLineWidth = SkMaxScalar(fMaxLineWidth, section.width());

        section.formatLinesByWords(width);

        // Get the stats
        fAlphabeticBaseline = section.alphabeticBaseline();
        fIdeographicBaseline = section.ideographicBaseline();
        fHeight += section.height();
        fWidth = SkMaxScalar(fWidth, section.width());
        fMaxIntrinsicWidth =
            SkMaxScalar(fMaxIntrinsicWidth, section.maxIntrinsicWidth());
        fMinIntrinsicWidth =
            SkMaxScalar(fMinIntrinsicWidth, section.minIntrinsicWidth());
    }

    fPicture = nullptr;

    return true;
}

void SkParagraph::paint(SkCanvas* canvas, double x, double y) {

    if (nullptr == fPicture) {
        // Postpone painting until we actually have to paint
        this->recordPicture();
    }

    SkMatrix matrix =
        SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
    canvas->drawPicture(fPicture, &matrix, nullptr);
}

void SkParagraph::recordPicture() {

    SkPictureRecorder recorder;
    SkCanvas* textCanvas = recorder.beginRecording(fWidth, fHeight, nullptr, 0);
    for (auto& section : fSections) {

        section.paintEachLineByStyles(textCanvas);
        textCanvas->translate(0, section.height());
    }

    fPicture = recorder.finishRecordingAsPicture();
}

void SkParagraph::breakTextIntoSections() {

    SkDebugf("breakTextIntoSections:\n");
    fSections.clear();

    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator
        * breakIterator(ubrk_open(UBRK_LINE, "th", nullptr, 0, &status));
    if (U_FAILURE(status)) {
        SkDebugf("Could not create break iterator: %s", u_errorName(status));
        SK_ABORT("");
    }

    UText utf8UText = UTEXT_INITIALIZER;
    utext_openUTF8(&utf8UText, fUtf8.begin(), fUtf8.size(), &status);
    std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>>
        autoClose(&utf8UText);
    if (U_FAILURE(status)) {
        SkDebugf("Could not create utf8UText: %s", u_errorName(status));
        return;
    }

    ubrk_setUText(breakIterator, &utf8UText, &status);
    if (U_FAILURE(status)) {
        SkDebugf("Could not setText on break iterator: %s",
                 u_errorName(status));
        return;
    }

    auto firstChar = (int32_t) fUtf8.size();
    auto lastChar = (int32_t) fUtf8.size();
    auto lastWordChar = lastChar;

    size_t firstStyle = fTextStyles.size() - 1;
    std::vector<SkSpan<const char>> softLineBreaks;
    while (lastChar > 0) {
        int32_t ubrkStatus = ubrk_preceding(breakIterator, firstChar);
        if (ubrkStatus == icu::BreakIterator::DONE) {
            // Take care of the first line; treat it as a hard line break
            firstChar = 0;
            ubrkStatus = UBRK_LINE_HARD;
        } else {
            firstChar = ubrkStatus;

            // Collect all soft line breaks for future use
            softLineBreaks.emplace(softLineBreaks.begin(), fUtf8.begin() + firstChar, lastWordChar - firstChar);
            lastWordChar = firstChar;
        }

        if (/*ubrk_getRuleStatus(breakIterator)*/ ubrkStatus != UBRK_LINE_HARD) {
            // Ignore soft line breaks for the rest
            continue;
        }

        // Remove all insignificant characters at the end of the line (whitespaces)
        // TODO: we keep at least one space in case the line is all spaces for now
        // TODO: since Flutter is using a space character to measure things;
        // TODO: need to fix it later
        while (lastChar > firstChar) {
            int32_t character = *(fUtf8.begin() + lastChar - 1);
            if (!u_isWhitespace(character)) {
                break;
            }
            lastChar -= 1;
        }

        // Find the first style that is related to the line
        while (firstStyle > 0
            && fTextStyles[firstStyle].fText.begin()
                > fUtf8.begin() + firstChar) {
            --firstStyle;
        }

        size_t lastStyle = firstStyle;
        while (lastStyle != fTextStyles.size()
            && fTextStyles[lastStyle].fText.begin()
                < fUtf8.begin() + lastChar) {
            ++lastStyle;
        }

        // Generate blocks for future use
        std::vector<StyledText> styles;
        styles.reserve(lastStyle - firstStyle);
        for (auto s = firstStyle; s < lastStyle; ++s) {
            auto& style = fTextStyles[s];

            auto start = SkTMax((int32_t) (style.fText.begin() - fUtf8.begin()),
                                firstChar);
            auto end = SkTMin((int32_t) (style.fText.end() - fUtf8.begin()), lastChar);
            styles.emplace_back(SkSpan<const char>(fUtf8.begin() + start, SkToS32(end - start)),
                                style.fStyle);
        }

        SkDebugf("Section [%d : %d] %d, %d\n", firstChar, lastChar, styles.size(), softLineBreaks.size());

        // Add one more string to the list;
        //fParagraphs.emplace(fParagraphs.begin(), fParagraphStyle, std::move(styles));
        fSections.emplace(fSections.begin(), fParagraphStyle, std::move(styles), std::move(softLineBreaks));
        softLineBreaks.clear();

        // Move on
        lastChar = firstChar;
    }
}

// TODO: implement properly (currently it only works as an indicator that something changed in the text)
std::vector<SkTextBox> SkParagraph::getRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rectHeightStyle,
    RectWidthStyle rectWidthStyle) {

    std::vector<SkTextBox> result;
    for (auto& section : fSections) {
        section.getRectsForRange(
            fUtf8.begin() + start,
            fUtf8.begin() + end,
            result);
    }

    return result;
}

SkPositionWithAffinity
SkParagraph::getGlyphPositionAtCoordinate(double dx, double dy) const {
    // TODO: implement
    return {0, Affinity::UPSTREAM};
}

SkRange<size_t> SkParagraph::getWordBoundary(unsigned offset) {
    // TODO: implement
    SkRange<size_t> result;
    return result;
}