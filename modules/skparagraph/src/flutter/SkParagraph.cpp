/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <unicode/brkiter.h>
#include "flutter/SkParagraph.h"
#include "SkPictureRecorder.h"

void printText(const std::string& label, const UChar* text, size_t start, size_t end) {
  icu::UnicodeString utf16 = icu::UnicodeString(text + start, end - start);
  std::string str;
  utf16.toUTF8String(str);
  SkDebugf("%s: %d:%d'%s'\n", label.c_str(), start, end, str.c_str());
}

SkParagraph::SkParagraph(const std::string& text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : _style(style)
    , _utf8(text.data(), text.size())
    , _picture(nullptr)
{
  std::transform(blocks.cbegin(), blocks.cend(), std::back_inserter(_styles), [this](const Block& value) {
    return StyledText( SkSpan<const char>(_utf8.begin() + value.start, value.end - value.start), value.textStyle);
  });
}

SkParagraph::SkParagraph(const std::u16string& utf16text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : _style(style)
    , _picture(nullptr)
{
  icu::UnicodeString unicode((UChar*)utf16text.data(), utf16text.size());
  std::string str;
  unicode.toUTF8String(str);
  _utf8 = SkSpan<const char>(str.data(), str.size());

  std::transform(blocks.cbegin(), blocks.cend(), std::back_inserter(_styles), [this](const Block& value) {
    return StyledText( SkSpan<const char>(_utf8.begin() + value.start, value.end - value.start), value.textStyle);
  });
}

SkParagraph::~SkParagraph() = default;

double SkParagraph::GetMaxWidth() {
  return SkScalarToDouble(_width);
}

double SkParagraph::GetHeight() {
  return SkScalarToDouble(_height);
}

double SkParagraph::GetMinIntrinsicWidth() {
  return SkScalarToDouble(_width/*_minIntrinsicWidth*/);
}

double SkParagraph::GetMaxIntrinsicWidth() {
  return SkScalarToDouble(_width /*_maxIntrinsicWidth*/);
}

double SkParagraph::GetAlphabeticBaseline() {
  return SkScalarToDouble(_alphabeticBaseline);
}

double SkParagraph::GetIdeographicBaseline() {
  // TODO: implement
  return SkScalarToDouble(_ideographicBaseline);
}

bool SkParagraph::Layout(double doubleWidth) {

  // Break the text into lines (with each one broken into blocks by style)
  BreakTextIntoParagraphs();

  // Collect Flutter values
  _alphabeticBaseline = 0;
  _height = 0;
  _width = 0;
  _ideographicBaseline = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
  _linesNumber = 0;

  auto width = SkDoubleToScalar(doubleWidth);

  // Take care of line limitation across all the paragraphs
  size_t maxLines = _style.getMaxLines();
  for (auto& paragraph : _paragraphs) {

    // Shape
    paragraph.layout(width, maxLines);

    // Make sure we haven't exceeded the limits
    _linesNumber += paragraph.lineNumber();
    if (!_style.unlimited_lines()) {
      maxLines -= paragraph.lineNumber();
    }
    if (maxLines <= 0) {
      break;
    }

    // Format
    paragraph.format(width);

    // Get the stats
    _alphabeticBaseline = 0;
    _ideographicBaseline = 0;
    _height += paragraph.height();
    _width = SkMaxScalar(_width, paragraph.width());
    _maxIntrinsicWidth = SkMaxScalar(_maxIntrinsicWidth, paragraph.maxIntrinsicWidth());
    _minIntrinsicWidth = SkMaxScalar(_minIntrinsicWidth, paragraph.minIntrinsicWidth());
  }

  RecordPicture();

  return true;
}

void SkParagraph::Paint(SkCanvas* canvas, double x, double y) const {

  SkMatrix matrix = SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
  canvas->drawPicture(_picture, &matrix, nullptr);
}

void SkParagraph::RecordPicture() {

  SkPictureRecorder recorder;
  SkCanvas* textCanvas = recorder.beginRecording(_width, _height, nullptr, 0);
  // Point will be moved on each paragraph
  SkPoint point = SkPoint::Make(0, 0);
  for (auto& paragraph : _paragraphs) {

    paragraph.paint(textCanvas, point);
  }

  _picture = recorder.finishRecordingAsPicture();
}

void SkParagraph::BreakTextIntoParagraphs() {

  _paragraphs.clear();

  UErrorCode status = U_ZERO_ERROR;
  UBreakIterator* breakIterator(ubrk_open(UBRK_LINE, "th", nullptr, 0, &status));
  if (U_FAILURE(status)) {
    SkDebugf("Could not create break iterator: %s", u_errorName(status));
    SK_ABORT("");
  }

  UText utf8UText = UTEXT_INITIALIZER;
  utext_openUTF8(&utf8UText, _utf8.begin(), _utf8.size(), &status);
  std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> autoClose(&utf8UText);
  if (U_FAILURE(status)) {
    SkDebugf("Could not create utf8UText: %s", u_errorName(status));
    return;
  }

  ubrk_setUText(breakIterator, &utf8UText, &status);
  if (U_FAILURE(status)) {
    SkDebugf("Could not setText on break iterator: %s", u_errorName(status));
    return;
  }

  auto firstChar = (int32_t)_utf8.size();
  auto lastChar = (int32_t)_utf8.size();

  size_t firstStyle = _styles.size() - 1;
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
      int32_t character = *(_utf8.begin() + lastChar - 1);
      if (!u_isWhitespace(character)) {
        break;
      }
      lastChar -= 1;
    }

    // Find the first style that is related to the line
    while (firstStyle > 0 && _styles[firstStyle].text.begin() > _utf8.begin() + firstChar) {
      --firstStyle;
    }

    size_t lastStyle = firstStyle;
    while (lastStyle != _styles.size() && _styles[lastStyle].text.begin() < _utf8.begin() + lastChar) {
      ++lastStyle;
    }

    // Generate blocks for future use
    std::vector<StyledText> styles;
    styles.reserve(lastStyle - firstStyle);
    for (auto s = firstStyle; s < lastStyle; ++s) {
      auto& style = _styles[s];

      auto start = SkTMax((int32_t)(style.text.begin() - _utf8.begin()), firstChar);
      auto end = SkTMin((int32_t)(style.text.end() - _utf8.begin()), lastChar);
      styles.emplace_back(SkSpan<const char>(_utf8.begin() + start, end - start), style.textStyle);
    }

    // Add one more string to the list;
    _paragraphs.emplace(_paragraphs.begin(), _style, std::move(styles));

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
    RectHeightStyle rect_height_style,
    RectWidthStyle rect_width_style) {
  std::vector<SkTextBox> result;
  for (auto& paragraph : _paragraphs) {
    paragraph.GetRectsForRange(_utf8.begin() + start, _utf8.begin() + end, result);
  }

  return result;
}

SkPositionWithAffinity SkParagraph::GetGlyphPositionAtCoordinate(double dx, double dy) const {
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