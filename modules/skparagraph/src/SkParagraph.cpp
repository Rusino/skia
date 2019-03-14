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

  class SkTextBreaker {

   public:
    SkTextBreaker() : fPos(-1) {
    }

    bool initialize(SkSpan<const char> text, UBreakIteratorType type) {
      UErrorCode status = U_ZERO_ERROR;

      UText utf8UText = UTEXT_INITIALIZER;
      utext_openUTF8(&utf8UText, text.begin(), text.size(), &status);
      fAutoClose =
          std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>>(&utf8UText);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create utf8UText: %s", u_errorName(status));
        return false;
      }
      fIterator = ubrk_open(type, "th", nullptr, 0, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create line break iterator: %s",
                 u_errorName(status));
        SK_ABORT("");
      }

      ubrk_setUText(fIterator, &utf8UText, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not setText on break iterator: %s",
                 u_errorName(status));
        return false;
      }
      return true;
    }

    size_t next(size_t pos) {
      fPos = ubrk_following(fIterator, SkToS32(pos));
      return fPos;
    }

    int32_t status() { return ubrk_getRuleStatus(fIterator); }

    bool eof() { return fPos == icu::BreakIterator::DONE; }

    ~SkTextBreaker() = default;

   private:
    std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> fAutoClose;
    UBreakIterator* fIterator;
    int32_t fPos;
  };

  auto removeLineBreak =
      [this](size_t start, size_t end) -> size_t {
        auto pos = end;
        while (pos > start) {
          auto ch = *(fUtf8.begin() + pos - 1);
          if (u_charType(ch) != U_CONTROL_CHAR) {
            break;
          }
          --pos;
        }
        return pos;
      };

  auto buildWords =
      [this](size_t start, size_t end) -> SkWords {
        auto pos = end;
        while (pos > start) {
          auto ch = *(fUtf8.begin() + pos - 1);
          if (!u_isspace(ch) &&
              u_charType(ch) != U_CONTROL_CHAR &&
              u_charType(ch) != U_NON_SPACING_MARK) {
            break;
          }

          --pos;
        }
        SkSpan<const char> text(fUtf8.begin() + start, pos - start);
        SkSpan<const char> spaces(fUtf8.begin() + pos, end - pos);
        return SkWords(text, spaces);
      };

  SkTextBreaker breaker;
  if (!breaker.initialize(fUtf8, UBRK_LINE)) {
    return;
  }

  size_t currentPos = 0;
  size_t linePos = 0;
  size_t wordPos = 0;
  SkTArray<SkWords, true> unbreakable;
  while (true) {

    currentPos = breaker.next(currentPos);
    if (breaker.eof()) {
      break;
    }

    if (currentPos != fUtf8.size() &&
      breaker.status() != UBRK_LINE_HARD) {
      unbreakable.emplace_back(buildWords(wordPos, currentPos));
      wordPos = currentPos;
      continue;
    }

    SkTArray<SkBlock, true> styles;
    {
      auto start = fTextStyles.begin();
      while (start != fTextStyles.end() && start->fEnd <= linePos) ++start;
      auto end = start;
      while (end != fTextStyles.end() && end->fStart < currentPos) ++end;
      styles.reserve(SkToS32(end - start));
      for (auto i = start; i != end; ++i) {
        styles.emplace_back(
            SkSpan<const char>(fUtf8.begin() + i->fStart, i->fEnd - i->fStart),
            &i->fStyle);
      }
    }

    // Remove a (possible) line break symbol in the end
    auto endPos = removeLineBreak(wordPos, currentPos);
    if (wordPos < endPos) {
      unbreakable.emplace_back(buildWords(wordPos, endPos));
    }
    fSections.emplace_back(
        std::make_unique<SkSection>(
            SkSpan<const char>(fUtf8.begin() + linePos, endPos - linePos),
            fParagraphStyle,
            std::move(styles),
            std::move(unbreakable)));

    linePos = currentPos;
    wordPos = currentPos;
    unbreakable.reset();
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