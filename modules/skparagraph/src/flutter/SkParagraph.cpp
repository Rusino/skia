/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <unicode/brkiter.h>
#include "flutter/SkParagraph.h"
#include "SkPictureRecorder.h"

void printText(const std::string& label,
               const UChar* text,
               size_t start,
               size_t end) {
    icu::UnicodeString utf16 = icu::UnicodeString(text + start, end - start);
    std::string str;
    utf16.toUTF8String(str);
    SkDebugf("%s: %d:%d'%s'\n", label.c_str(), start, end, str.c_str());
}

SkParagraph::SkParagraph(const std::string& text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : fParagraphStyle(style), _fUtf8(text.data(), text.size()), fPicture(nullptr) {
    std::transform(blocks.cbegin(),
                   blocks.cend(),
                   std::back_inserter(fTextStyles),
                   [this](const Block& value) {
                       return StyledText(SkSpan<const char>(
                           _fUtf8.begin() + value.start,
                           value.end - value.start), value.textStyle);
                   });
}

SkParagraph::SkParagraph(const std::u16string& utf16text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : fParagraphStyle(style), fPicture(nullptr) {
    icu::UnicodeString unicode((UChar*) utf16text.data(), utf16text.size());
    std::string str;
    unicode.toUTF8String(str);
    _fUtf8 = SkSpan<const char>(str.data(), str.size());

    std::transform(blocks.cbegin(),
                   blocks.cend(),
                   std::back_inserter(fTextStyles),
                   [this](const Block& value) {
                       return StyledText(SkSpan<const char>(
                           _fUtf8.begin() + value.start,
                           value.end - value.start), value.textStyle);
                   });
}

SkParagraph::~SkParagraph() = default;

double SkParagraph::GetMaxWidth() {
    return SkScalarToDouble(fWidth);
}

double SkParagraph::GetHeight() {
    return SkScalarToDouble(fHeight);
}

double SkParagraph::GetMinIntrinsicWidth() {
    return SkScalarToDouble(fWidth/*_minIntrinsicWidth*/);
}

double SkParagraph::GetMaxIntrinsicWidth() {
    return SkScalarToDouble(fWidth /*_maxIntrinsicWidth*/);
}

double SkParagraph::GetAlphabeticBaseline() {
    return SkScalarToDouble(fAlphabeticBaseline);
}

double SkParagraph::GetIdeographicBaseline() {
    // TODO: implement
    return SkScalarToDouble(fIdeographicBaseline);
}

bool SkParagraph::Layout(double doubleWidth) {

    // Break the text into lines (with each one broken into blocks by style)
    BreakTextIntoParagraphs();

    // Collect Flutter values
    fAlphabeticBaseline = 0;
    fHeight = 0;
    fWidth = 0;
    fIdeographicBaseline = 0;
    fMaxIntrinsicWidth = 0;
    fMinIntrinsicWidth = 0;
    fLinesNumber = 0;

    auto width = SkDoubleToScalar(doubleWidth);

    // Take care of line limitation across all the paragraphs
    size_t maxLines = fParagraphStyle.getMaxLines();
    for (auto& paragraph : fParagraphs) {

        // Shape
        paragraph.layout(width, maxLines);

        // Make sure we haven't exceeded the limits
        fLinesNumber += paragraph.lineNumber();
        if (!fParagraphStyle.unlimited_lines()) {
            maxLines -= paragraph.lineNumber();
        }
        if (maxLines <= 0) {
            break;
        }

        // Format
        paragraph.format(width);

        // Get the stats
        fAlphabeticBaseline = 0;
        fIdeographicBaseline = 0;
        fHeight += paragraph.height();
        fWidth = SkMaxScalar(fWidth, paragraph.width());
        fMaxIntrinsicWidth =
            SkMaxScalar(fMaxIntrinsicWidth, paragraph.maxIntrinsicWidth());
        fMinIntrinsicWidth =
            SkMaxScalar(fMinIntrinsicWidth, paragraph.minIntrinsicWidth());
    }

    RecordPicture();

    return true;
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) const {

    SkMatrix
        matrix = SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
    canvas->drawPicture(fPicture, &matrix, nullptr);
}

void SkParagraph::RecordPicture() {

    SkPictureRecorder recorder;
    SkCanvas* textCanvas = recorder.beginRecording(fWidth, fHeight, nullptr, 0);
    // Point will be moved on each paragraph
    SkPoint point = SkPoint::Make(0, 0);
    for (auto& paragraph : fParagraphs) {

        paragraph.paint(textCanvas, point);
        point.fX = 0;
        point.fY += paragraph.height();
    }

    fPicture = recorder.finishRecordingAsPicture();
}

void SkParagraph::BreakTextIntoParagraphs() {

    fParagraphs.clear();

    UErrorCode status = U_ZERO_ERROR;
    UBreakIterator
        * breakIterator(ubrk_open(UBRK_LINE, "th", nullptr, 0, &status));
    if (U_FAILURE(status)) {
        SkDebugf("Could not create break iterator: %s", u_errorName(status));
        SK_ABORT("");
    }

    UText utf8UText = UTEXT_INITIALIZER;
    utext_openUTF8(&utf8UText, _fUtf8.begin(), _fUtf8.size(), &status);
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

    auto firstChar = (int32_t) _fUtf8.size();
    auto lastChar = (int32_t) _fUtf8.size();

    size_t firstStyle = fTextStyles.size() - 1;
    while (lastChar > 0) {
        int32_t ubrkStatus = ubrk_preceding(breakIterator, firstChar);
        if (ubrkStatus == icu::BreakIterator::DONE) {
            // Take care of the first line
            firstChar = 0;
        } else {
            firstChar = ubrkStatus;
            if (ubrk_getRuleStatus(breakIterator) != UBRK_LINE_HARD) {
                continue;
            }
        }

        // Remove all insignificant characters at the end of the line (whitespaces)
        // TODO: we keep at least one space in case the line is all spaces for now
        // TODO: since Flutter is using a space character to measure things;
        // TODO: need to fix it later
        while (lastChar > firstChar) {
            int32_t character = *(_fUtf8.begin() + lastChar - 1);
            if (!u_isWhitespace(character)) {
                break;
            }
            lastChar -= 1;
        }

        // Find the first style that is related to the line
        while (firstStyle > 0
            && fTextStyles[firstStyle].fText.begin() > _fUtf8.begin() + firstChar) {
            --firstStyle;
        }

        size_t lastStyle = firstStyle;
        while (lastStyle != fTextStyles.size()
            && fTextStyles[lastStyle].fText.begin() < _fUtf8.begin() + lastChar) {
            ++lastStyle;
        }

        // Generate blocks for future use
        std::vector<StyledText> styles;
        styles.reserve(lastStyle - firstStyle);
        for (auto s = firstStyle; s < lastStyle; ++s) {
            auto& style = fTextStyles[s];

            auto start = SkTMax((int32_t) (style.fText.begin() - _fUtf8.begin()),
                                firstChar);
            auto end =
                SkTMin((int32_t) (style.fText.end() - _fUtf8.begin()), lastChar);
            styles.emplace_back(SkSpan<const char>(_fUtf8.begin() + start,
                                                   end - start),
                                style.fStyle);
        }

        // Add one more string to the list;
        fParagraphs.emplace(fParagraphs.begin(), fParagraphStyle, std::move(styles));

        // Move on
        lastChar = firstChar;
    }
/*
  SkDebugf("Paragraphs:\n");
  // Print all lines
  size_t linenum = 0;
  for (auto& line : _paragraphs) {
    ++linenum;
    line.printBlocks(linenum);
  }
*/
}

// TODO: implement properly (currently it only works as an indicator that something changed in the text)
std::vector<SkTextBox> SkParagraph::GetRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rectHeightStyle,
    RectWidthStyle rectWidthStyle) {
    std::vector<SkTextBox> result;
    for (auto& paragraph : fParagraphs) {
        paragraph.GetRectsForRange(_fUtf8.begin() + start,
                                   _fUtf8.begin() + end,
                                   result);
    }

    return result;
}

SkPositionWithAffinity
SkParagraph::GetGlyphPositionAtCoordinate(double dx, double dy) const {
    // TODO: implement
    //SkASSERT(false);
    return SkPositionWithAffinity(0, Affinity::UPSTREAM);
}

SkRange<size_t> SkParagraph::GetWordBoundary(unsigned offset) {
    // TODO: implement
    SkASSERT(false);
    SkRange<size_t> result;
    return result;
}