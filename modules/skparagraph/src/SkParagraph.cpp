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
#include "SkSection.h"

SkParagraph::SkParagraph(const std::string& text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : fParagraphStyle(std::move(style)), fUtf8(text.data(), text.size()),
      fPicture(nullptr) {

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
    : fParagraphStyle(std::move(style)), fPicture(nullptr) {

  icu::UnicodeString
      unicode((UChar*) utf16text.data(), SkToS32(utf16text.size()));
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

std::string toString(SkSpan<const char> text) {
  icu::UnicodeString utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
  std::string str;
  utf16.toUTF8String(str);
  return str;
}

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

    section->shapeIntoLines(width, maxLines);

    // Make sure we haven't exceeded the limits
    fLinesNumber += section->lineNumber();
    if (!fParagraphStyle.unlimited_lines()) {
      maxLines -= section->lineNumber();
    }
    if (maxLines <= 0) {
      break;
    }

    section->formatLinesByWords(width);
    fMaxLineWidth = SkMaxScalar(fMaxLineWidth, section->width());

    // Get the stats
    fAlphabeticBaseline = section->alphabeticBaseline();
    fIdeographicBaseline = section->ideographicBaseline();
    fHeight += section->height();
    fWidth = SkMaxScalar(fWidth, section->width());
    fMaxIntrinsicWidth =
        SkMaxScalar(fMaxIntrinsicWidth, section->maxIntrinsicWidth());
    fMinIntrinsicWidth =
        SkMaxScalar(fMinIntrinsicWidth, section->minIntrinsicWidth());
  }

  SkDebugf("fHeight: %f\n", fHeight);
  SkDebugf("fWidth: %f\n", fWidth);
  SkDebugf("fMaxIntrinsicWidth: %f\n", fMaxIntrinsicWidth);
  SkDebugf("fMinIntrinsicWidth: %f\n", fMinIntrinsicWidth);
  SkDebugf("fLinesNumber: %d\n", fLinesNumber);
  SkDebugf("fMaxLineWidth: %f\n", fMaxLineWidth);

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

    section->paintEachLineByStyles(textCanvas);
    textCanvas->translate(0, section->height());
  }

  fPicture = recorder.finishRecordingAsPicture();
}

void SkParagraph::breakTextIntoSections() {

  class BreakIterator {

   public:
    explicit BreakIterator(SkSpan<const char> text) {

      UErrorCode status = U_ZERO_ERROR;

      UText utf8UText = UTEXT_INITIALIZER;
      utext_openUTF8(&utf8UText, text.begin(), text.size(), &status);
      fAutoClose =
          std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>>(
              &utf8UText);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create utf8UText: %s", u_errorName(status));
        return;
      }

      fLineBreak = ubrk_open(UBRK_LINE, "th", nullptr, 0, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create line break iterator: %s",
                 u_errorName(status));
        SK_ABORT("");
      }

      ubrk_setUText(fLineBreak, &utf8UText, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not setText on break iterator: %s",
                 u_errorName(status));
        return;
      }

      fWordBreak = ubrk_open(UBRK_WORD, "th", nullptr, 0, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create word break iterator: %s",
                 u_errorName(status));
        SK_ABORT("");
      }

      ubrk_setUText(fWordBreak, &utf8UText, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not setText on break iterator: %s",
                 u_errorName(status));
        return;
      }

      fCurrentPosition = 0;
      fNextLinePosition = 0;
      fNextWordPosition = 0;
      fWord = SkSpan<const char>(text.begin(), 0);
      fLine = SkSpan<const char>(text.begin(), 0);

      fText = text;
    }

    bool next() {

      auto isWhiteSpaces = [](const char* start, int32_t size) -> bool {
        auto pos = start + size - 1;
        while (pos >= start) {
          auto ch = *pos;
          if (!u_isspace(ch) &&
              u_charType(ch) != U_CONTROL_CHAR &&
              u_charType(ch) != U_NON_SPACING_MARK) {
            break;
          }
          --pos;
        }
        return pos < start;
      };

      auto trimControls = [this](int32_t start, int32_t end) -> SkSpan<const char> {

        SkASSERT(start <= end);
        while (end > start) {
          auto ch = *(this->fText.begin() + end - 1);
          if (u_charType(ch) != U_CONTROL_CHAR) {
            break;
          }
          --end;
        }

        return SkSpan<const char>(this->fText.begin() + start, SkToU32(end - start));
      };

      auto currentChar = fText.begin() + fCurrentPosition;
      if (currentChar == fText.end()) {
        return false;
      }

      if (fNextLinePosition <= fCurrentPosition) {
        // Move line iterator if necessary
        while (true) {
          fNextLinePosition = ubrk_following(fLineBreak, fNextLinePosition);
          if (fNextLinePosition == icu::BreakIterator::DONE) {
            // We really should stop before
            SkASSERT(false);
          } else if (fNextLinePosition == SkToS32(fText.size())) {
            // End of the text is a hard line break
            break;
          } else if (ubrk_getRuleStatus(fLineBreak) == UBRK_LINE_HARD) {
            // End of line
            break;
          }
          // Ignore soft line breaks
        }
        fLine = SkSpan<const char>(currentChar, SkToU32(fNextLinePosition - fCurrentPosition));
      }

      if (fNextWordPosition <= fCurrentPosition) {
        // Move word iterator if necessary
        fNextWordPosition = ubrk_following(fWordBreak, fNextWordPosition);
        if (fNextWordPosition == icu::BreakIterator::DONE) {
          // End of text as end of word
          fNextWordPosition = SkToS32(fText.size());
        }

        fWord = trimControls(fCurrentPosition, fNextWordPosition);
        fTrailingSpaces = SkSpan<const char>(fWord.end(), 0);
        if (!isWhiteSpaces(currentChar, fNextWordPosition - fCurrentPosition)) {
            if (fNextWordPosition < fNextLinePosition) {
                // Look ahead if possible
                auto nextNextPosition = ubrk_following(fWordBreak, fNextWordPosition);
                if (nextNextPosition == icu::BreakIterator::DONE ||
                    nextNextPosition > fNextLinePosition) {
                  // Next word is behind the line break
                } else {
                  if (isWhiteSpaces(fText.begin() + fNextWordPosition,
                                    nextNextPosition - fNextWordPosition)) {
                    // Add extra spaces to the current word and move the position
                    fTrailingSpaces = trimControls(fNextWordPosition, nextNextPosition);
                    fNextWordPosition = nextNextPosition;
                  }
                }
            }
        } else {
          // This is one tricky case when the word itself is all spaces (leading spaces)
          // It takes care of itself, though...
        }
      }
      fCurrentPosition = SkTMin(fNextWordPosition, fNextLinePosition);

      return true;
    }

    inline SkSpan<const char> getLine() { return fLine; }
    inline SkSpan<const char> getWord() { return fWord; }
    inline SkSpan<const char> getTrailingSpaces() { return fTrailingSpaces; }

    inline bool isWordBreak() { return fCurrentPosition == fNextWordPosition; }
    inline bool isLineBreak() { return fCurrentPosition == fNextLinePosition; }

   private:
    UBreakIterator* fLineBreak;
    UBreakIterator* fWordBreak;

    int32_t fCurrentPosition;
    int32_t fNextLinePosition;
    int32_t fNextWordPosition;

    SkSpan<const char> fWord;
    SkSpan<const char> fTrailingSpaces;
    SkSpan<const char> fLine;
    SkSpan<const char> fText;
    std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> fAutoClose;
  };

  fSections.reset();
  SkTArray<SkWord, true> words;

  BreakIterator breakIterator(fUtf8);
  bool lineBreakBefore = true;
  while (breakIterator.next()) {

    if (breakIterator.isWordBreak()) {
      auto word = breakIterator.getWord();
      auto spaces = breakIterator.getTrailingSpaces();
      if (!word.empty() || !spaces.empty()) {
        words.emplace_back(word, spaces, lineBreakBefore);
        lineBreakBefore = !spaces.empty();
      }

      if (!breakIterator.isLineBreak()) {
        continue;
      }
    }

    // Line break situation
    SkASSERT(breakIterator.isLineBreak());
    auto line = breakIterator.getLine();
    SkDebugf("Section [%d : %d] %d\n",
             line.begin() - &fUtf8[0],
             line.end() - &fUtf8[0],
             words.size());

    // Copy all the styles (corrected)
    auto limits = SkSection::selectStyles(line, SkSpan<StyledText>(fTextStyles));

    SkTArray<StyledText> styles;
    styles.reserve(SkToS32(limits.size()));

    for (auto style = limits.begin(); style < limits.end(); ++style) {
      auto start = SkTMax(style->fText.begin(), line.begin());
      auto end = SkTMin(style->fText.end(), line.end());
      styles.emplace_back(SkSpan<const char>(start, SkToU32(end - start)), style->fStyle);
    }
    fSections.emplace_back(std::make_unique<SkSection>(line, fParagraphStyle, std::move(styles), std::move(words)));
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
    section->getRectsForRange(
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