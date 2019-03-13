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
#include "SkBlock.h"

SkParagraph::SkParagraph(const std::string& text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : fParagraphStyle(style)
    , fTextStyles(std::move(blocks))
    , fUtf8(text.data(), text.size()),
      fPicture(nullptr) {
}

SkParagraph::SkParagraph(const std::u16string& utf16text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : fParagraphStyle(style)
    , fTextStyles(std::move(blocks))
    , fPicture(nullptr) {

  icu::UnicodeString
      unicode((UChar*) utf16text.data(), SkToS32(utf16text.size()));
  std::string str;
  unicode.toUTF8String(str);
  fUtf8 = SkSpan<const char>(str.data(), str.size());
}

SkParagraph::~SkParagraph() = default;

void SkParagraph::resetContext() {
  
  fAlphabeticBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fIdeographicBaseline = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
  fLinesNumber = 0;
  fMaxLineWidth = 0;

  fPicture = nullptr;
  fSections.reset();
}

void SkParagraph::updateStats(const SkSection& section) {

  fAlphabeticBaseline = section.alphabeticBaseline();
  fIdeographicBaseline = section.ideographicBaseline();
  fHeight += section.height();
  fWidth = SkMaxScalar(fWidth, section.width());
  fMaxLineWidth = SkMaxScalar(fMaxLineWidth, section.width());
  fMaxIntrinsicWidth = SkMaxScalar(fMaxIntrinsicWidth, section.maxIntrinsicWidth());
  fMinIntrinsicWidth = SkMaxScalar(fMinIntrinsicWidth, section.minIntrinsicWidth());
}

bool SkParagraph::layout(double doubleWidth) {
  
  this->resetContext();

  this->breakTextIntoSectionsAndWords();

  auto width = SkDoubleToScalar(doubleWidth);
  for (auto& section : fSections) {

    section->shapeIntoLines(width, this->linesLeft());
    
    if (!this->addLines(section->lineNumber())) {
      // Make sure we haven't exceeded the limits
      break;
    }

    section->formatLinesByWords(SkDoubleToScalar(doubleWidth));

    this->updateStats(*section);
  }

  return true;
}

void SkParagraph::paint(SkCanvas* canvas, double x, double y) {

  if (nullptr == fPicture) {
    // Postpone painting until we actually have to paint (or never)
    this->recordPicture();
  }

  SkMatrix matrix = SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
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

// TODO: We can be smarter and check soft line breaks against glyph clusters (much later)
void SkParagraph::breakTextIntoSectionsAndWords() {

  UErrorCode status = U_ZERO_ERROR;

  UText utf8UText = UTEXT_INITIALIZER;
  utext_openUTF8(&utf8UText, fUtf8.begin(), fUtf8.size(), &status);
  std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>>
        fAutoClose((&utf8UText));
  if (U_FAILURE(status)) {
    SkDebugf("Could not create utf8UText: %s", u_errorName(status));
    return;
  }

  UBreakIterator* breakIter = ubrk_open(UBRK_LINE, "th", nullptr, 0, &status);
  if (U_FAILURE(status)) {
    SkDebugf("Could not create line break iterator: %s",
             u_errorName(status));
    SK_ABORT("");
  }

  ubrk_setUText(breakIter, &utf8UText, &status);
  if (U_FAILURE(status)) {
    SkDebugf("Could not setText on break iterator: %s",
             u_errorName(status));
    return;
  }

  auto removeLineBreak =
      [](const char* start, size_t& end) {
        while (end != 0) {
          auto ch = *(start + end - 1);
          if (u_charType(ch) != U_CONTROL_CHAR) {
            break;
          }
          --end;
        }
      };

  auto removeWhitespaces =
      [](const char* start, size_t& end) -> SkSpan<const char> {
        if (end == 0) { return SkSpan<const char>(); }
        auto last = end;
        while (--end != 0) {
          auto ch = *(start + end);
          if (!u_isspace(ch) &&
              u_charType(ch) != U_CONTROL_CHAR &&
              u_charType(ch) != U_NON_SPACING_MARK) {
            break;
          }
        }
        ++end;
        return SkSpan<const char>(start + end, last - end);
      };

  SkSpan<const char> line;
  SkSpan<const char> words;
  SkSpan<const char> spaces;
  SkTArray<SkWords, true> unbreakable;

  const char* lineStart = fUtf8.begin();
  const char* wordsStart = fUtf8.begin();
  int32_t currentPos = 0;
  while (true) {

    currentPos = ubrk_following(breakIter, currentPos);
    if (currentPos == icu::BreakIterator::DONE) {
      break;
    }

    auto status = ubrk_getRuleStatus(breakIter);
    if (currentPos == SkToS32(fUtf8.size())) {
      status = UBRK_LINE_HARD;
    }

    // Any break is good enough for words
    // TODO: soft line break is not a word break but good enough for formatting. For now...
    size_t endPos = currentPos - (wordsStart - fUtf8.begin());
    removeLineBreak(wordsStart, endPos);
    spaces = removeWhitespaces(wordsStart, endPos);
    words = SkSpan<const char>(wordsStart, endPos);
    wordsStart = fUtf8.begin() + currentPos;

    if (!words.empty() || !spaces.empty()) {
      unbreakable.emplace_back(words, spaces);
    }

    if (status != UBRK_LINE_HARD) {
      continue;
    }

    // Remove a (possible) line break symbol in the end
    endPos = currentPos - (lineStart - fUtf8.begin());
    removeLineBreak(lineStart, endPos);
    line = SkSpan<const char>(lineStart, endPos);
    lineStart = fUtf8.begin() + currentPos;

    // Copy all the styles (corrected)
    auto start = fTextStyles.begin();
    while (start != fTextStyles.end() && start->fEnd <= size_t(line.begin() - fUtf8.begin())) {
      ++start;
    }
    auto end = start;
    while (end != fTextStyles.end() && end->fStart < size_t(line.end() - fUtf8.begin())) {
      ++end;
    }
    SkTArray<SkBlock, true> styles;
    styles.reserve(SkToS32(end - start));
    for (auto i = start; i != end; ++i) {
      styles.emplace_back(
          SkSpan<const char>(fUtf8.begin() + i->fStart, i->fEnd - i->fStart),
          &i->fStyle);
    }

    fSections.emplace_back(
        std::make_unique<SkSection>(
            line,
            fParagraphStyle,
            std::move(styles),
            std::move(unbreakable)));
    wordsStart = lineStart;
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

SkPositionWithAffinity SkParagraph::getGlyphPositionAtCoordinate(double dx, double dy) const {
  // TODO: implement
  return {0, Affinity::UPSTREAM};
}

SkRange<size_t> SkParagraph::getWordBoundary(unsigned offset) {
  // TODO: implement
  SkRange<size_t> result;
  return result;
}