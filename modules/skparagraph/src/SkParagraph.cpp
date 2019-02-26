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

  class BreakIterator {

   public:
    BreakIterator(SkSpan<const char> text) {
      UErrorCode status = U_ZERO_ERROR;

      UText utf8UText = UTEXT_INITIALIZER;
      utext_openUTF8(&utf8UText, text.begin(), text.size(), &status);
      fAutoClose = std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>>(&utf8UText);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create utf8UText: %s", u_errorName(status));
        return;
      }

      fLineBreak = ubrk_open(UBRK_LINE, "th", nullptr, 0, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create line break iterator: %s", u_errorName(status));
        SK_ABORT("");
      }

      fWordBreak = ubrk_open(UBRK_WORD, "th", nullptr, 0, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create word break iterator: %s", u_errorName(status));
        SK_ABORT("");
      }

      if (U_FAILURE(status)) {
        SkDebugf("Could not create utf8UText: %s", u_errorName(status));
        return;
      }

      ubrk_setUText(fLineBreak, &utf8UText, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not setText on break iterator: %s",u_errorName(status));
        return;
      }

      ubrk_setUText(fWordBreak, &utf8UText, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not setText on break iterator: %s", u_errorName(status));
        return;
      }

      fText = text;
      fCurrentIndex = 0;
      fNextLineIndex = -1;
      fNextWordIndex = -1;
      fPrevWordIndex = -1;
      bWordBreak = false;
      bLineBreak = false;
    }

    bool next() {

      if (fCurrentIndex == (int32_t)fText.size()) {
        return false;
      }
      // Move iterators forward if necessary
      if (fNextLineIndex <= fCurrentIndex) {
        // Ignore soft line breaks
        fPrevLineIndex = fCurrentIndex;
        fNextLineIndex = fCurrentIndex;
        while (true) {
          fNextLineIndex = ubrk_following(fLineBreak, fNextLineIndex);
          if (fNextLineIndex == icu::BreakIterator::DONE) {
            fNextLineIndex = fText.size();
            break;
          } else if (ubrk_getRuleStatus(fLineBreak) == UBRK_LINE_HARD) {
            break;
          }
        }
      }

      if (fNextWordIndex <= fCurrentIndex) {
        fPrevWordIndex = fCurrentIndex;
        fNextWordIndex = ubrk_following(fWordBreak, fCurrentIndex);
        if (fNextWordIndex == icu::BreakIterator::DONE) {
          fNextWordIndex = fText.size();
        }
      }

      fCurrentIndex = SkTMin(fNextWordIndex, fNextLineIndex);
      bWordBreak = fNextWordIndex <= fNextLineIndex;
      bLineBreak = fNextLineIndex <= fNextWordIndex;

      return true;
    }

    SkSpan<const char> getWord() {
      // Remove all insignificant characters at the end of the line (linebreaks)
      auto first = fPrevWordIndex;
      auto last = fNextWordIndex - 1;
      while (last >= first) {
        int32_t character = *(fText.begin() +  last);
        if (u_charType(character) != U_CONTROL_CHAR &&
            u_charType(character) != U_NON_SPACING_MARK) {
          break;
        }
        --last;
      }
      return SkSpan<const char>(fText.begin() + first, last - first + 1);
    }

    SkSpan<const char> getLine() {
      // Remove all insignificant characters at the end of the line (whitespaces)
      auto first = fPrevLineIndex;
      auto last = fNextLineIndex - 1;
      while (last >= first) {
        int32_t character = *(fText.begin() +  last);
        if (u_charType(character) != U_CONTROL_CHAR &&
            u_charType(character) != U_NON_SPACING_MARK) {
          break;
        }
        --last;
      }
      return SkSpan<const char>(fText.begin() + first, last - first + 1);
    }

    inline int32_t getRuleStatus() { return fRuleStatus; }
    inline int32_t getCurrentIndex() { return fCurrentIndex; }
    inline bool isWordBreak() { return bWordBreak; }
    inline bool isLineBreak() { return bLineBreak; }

   private:
    UBreakIterator* fLineBreak;
    UBreakIterator* fWordBreak;
    int32_t fCurrentIndex;
    int32_t fNextLineIndex;
    int32_t fPrevLineIndex;
    int32_t fNextWordIndex;
    int32_t fPrevWordIndex;
    int32_t fRuleStatus;
    bool bWordBreak;
    bool bLineBreak;
    SkSpan<const char> fText;
    std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> fAutoClose;
  };

    SkDebugf("breakTextIntoSections:\n");
    fSections.clear();

    BreakIterator breakIterator(fUtf8);
    size_t firstStyle = fTextStyles.size() - 1;
    std::vector<SkSpan<const char>> wordBreaks;

    while (breakIterator.next()) {

      if (breakIterator.isWordBreak()){
        // We only need word breaks to collect
        auto word = breakIterator.getWord();
        if (!word.empty()) {
          wordBreaks.emplace_back(breakIterator.getWord());
        }
        if (!breakIterator.isLineBreak()) {
          continue;
        }
      }

      // Line break situation
      SkASSERT(breakIterator.isLineBreak());
      auto line = breakIterator.getLine();

      // Find the first style that is related to the line;
      while (firstStyle > 0 && fTextStyles[firstStyle].fText.begin() > line.begin()) {
          --firstStyle;
      }

      // Find the last style that is related to the line
      size_t lastStyle = firstStyle;
      while (lastStyle != fTextStyles.size() && fTextStyles[lastStyle].fText.begin() < line.end()) {
          ++lastStyle;
      }

      // Generate blocks for future use
      std::vector<StyledText> styles;
      styles.reserve(lastStyle - firstStyle);
      for (auto s = firstStyle; s < lastStyle; ++s) {
          auto& style = fTextStyles[s];


          auto start = SkTMax(style.fText.begin(), line.begin());
          auto end = SkTMin(style.fText.end(), line.end());
          styles.emplace_back(SkSpan<const char>(start, SkToS32(end - start)), style.fStyle);
      }

      SkDebugf("Section [%d : %d] %d, %d\n", line.begin() - &fUtf8[0], line.end() - &fUtf8[0], styles.size(), wordBreaks.size());

      // Add one more section to the list
      fSections.emplace_back(line, fParagraphStyle, std::move(styles), std::move(wordBreaks));
      wordBreaks.clear();
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