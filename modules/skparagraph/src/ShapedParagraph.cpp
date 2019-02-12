/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ShapedParagraph.h"
#include "SkFontMetrics.h"

ShapedParagraph::ShapedParagraph(SkParagraphStyle style, std::vector<StyledText> styles)
: _style(style)
, _styles(std::move(styles))
{
  _alphabeticBaseline = 0;
  _ideographicBaseline = 0;
  _height = 0;
  _width = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
  _exceededLimits = false;

  _lines.emplace_back();
}

void ShapedParagraph::layout(SkScalar maxWidth, size_t maxLines) {

  class MultipleFontRunIterator final : public FontRunIterator {
   public:
    MultipleFontRunIterator(SkSpan<const char> utf8,
                            std::vector<StyledText>::iterator begin,
                            std::vector<StyledText>::iterator end,
                            SkTextStyle defaultStyle)
        : fText(utf8)
        , fCurrent(utf8.begin())
        , fEnd(utf8.end())
        , fCurrentStyle(SkTextStyle())
        , fDefaultStyle(defaultStyle)
        , fIterator(begin)
        , fNext(begin)
        , fLast(end)
    {
      fCurrentTypeface = SkTypeface::MakeDefault();
      MoveToNext();
    }

    void consume() override {

      if (fIterator == fLast) {
        fCurrent = fEnd;
        fCurrentStyle = fDefaultStyle;
      } else {
        fCurrent = fNext == fLast ? fEnd : std::next(fCurrent, fNext->text.begin() - fIterator->text.begin());
        fCurrentStyle = fIterator->textStyle;
      }

      fCurrentTypeface = fCurrentStyle.getTypeface();
      fFont = SkFont(fCurrentTypeface, fCurrentStyle.getFontSize());

      MoveToNext();
    }
    const char* endOfCurrentRun() const override {
      return fCurrent;
    }
    bool atEnd() const override {
      return fCurrent == fEnd;
    }

    const SkFont* currentFont() const override {
      return &fFont;
    }

    void MoveToNext() {

      fIterator = fNext;
      if (fIterator == fLast) {
        return;
      }
      // This is a semi-solution allows flutter to run correctly:
      // we break runs on every style change even if the font is still the same
      ++fNext;
    }

   private:
    SkSpan<const char> fText;
    const char* fCurrent;
    const char* fEnd;
    SkFont fFont;
    SkTextStyle fCurrentStyle;
    SkTextStyle fDefaultStyle;
    std::vector<StyledText>::iterator fIterator;
    std::vector<StyledText>::iterator fNext;
    std::vector<StyledText>::iterator fLast;
    sk_sp<SkTypeface> fCurrentTypeface;
  };

  _maxLines = maxLines;

  if (!_styles.empty()) {
    auto start = _styles.begin()->text.begin();
    auto end = _styles.empty() ? start - 1 : std::prev(_styles.end())->text.end();
    if (start < end) {
      SkSpan<const char> run(start, end - start);
      MultipleFontRunIterator font(run,
                                   _styles.begin(),
                                   _styles.end(),
                                   _style.getTextStyle());
      SkShaper shaper(nullptr);
      shaper.shape(this, &font, start, end - start, true, {0, 0}, maxWidth);
      return;
    }

    // Shaper does not shape empty lines
    SkFontMetrics metrics;
    _styles.back().textStyle.getFontMetrics(metrics);
    _alphabeticBaseline = - metrics.fAscent;
    _ideographicBaseline = - metrics.fAscent;
    _height = metrics.fDescent + metrics.fLeading - metrics.fAscent;
    _width = 0;
    _maxIntrinsicWidth = 0;
    _minIntrinsicWidth = 0;
    return;
  }

  // Shaper does not shape empty lines
  _height = 0;
  _width = 0;
  _maxIntrinsicWidth = 0;
  _minIntrinsicWidth = 0;
}

void ShapedParagraph::printBlocks(size_t linenum) {
  SkDebugf("Paragraph #%d\n", linenum);
  if (!_styles.empty()) {
    SkDebugf("Lost blocks\n");
    for (auto& block : _styles) {
      std::string str(block.text.begin(), block.text.size());
      SkDebugf("Block: '%s'\n", str.c_str());
    }
  }
  int i = 0;
  for (auto& line : _lines) {
    SkDebugf("Line: %d (%d)\n", i, line.words().size());
    for (auto& word : line.words()) {
      std::string str(word.text().begin(), word.text().size());
      SkDebugf("Block: '%s'\n", str.c_str());
    }
    ++i;
  }
}

void ShapedParagraph::format(SkScalar maxWidth) {

  size_t lineIndex = 0;
  for (auto& line : _lines) {

    ++lineIndex;
    SkScalar delta = maxWidth - line.advance().fX;
    if (delta <= 0) {
      // Delta can be < 0 if there are extra whitespaces at the end of the line;
      // This is a limitation of a current version
      continue;
    }

    switch (_style.effective_align()) {
      case SkTextAlign::left:
        break;
      case SkTextAlign::right:
        for (auto& word : line.words()) {
          word.shift(delta);
        }
        line.advance().fX = maxWidth;
        _width = maxWidth;
        break;
      case SkTextAlign::center: {
        auto half = delta / 2;
        for (auto& word : line.words()) {
          word.shift(half);
        }
        line.advance().fX = maxWidth;
        _width = maxWidth;
        break;
      }
      case SkTextAlign::justify: {
        if (&line == &_lines.back()) {
          break;
        }
        SkScalar step = delta / (line.words().size() - 1);
        SkScalar shift = 0;
        for (auto& word : line.words()) {
          word.shift(shift);
          if (&word != &line.words().back()) {
            word.expand(step);
            line.advance().fX = maxWidth;
            _width = maxWidth;
          }
          shift += step;
        }
        break;
      }
      default:
        break;
    }
  }
}

// TODO: currently we pick the first style of the run and go with it regardless
void ShapedParagraph::paint(SkCanvas* textCanvas, SkPoint& point) {

  std::vector<StyledText>::iterator firstStyle = _styles.begin();
  for (auto& line : _lines) {
    for (auto word : line.words()) {

      // Find the first style that affects the run
      while (firstStyle != _styles.end() && firstStyle->text.begin() < word.text().begin()) {
        ++firstStyle;
      }

      word.Paint(textCanvas, firstStyle == _styles.end() ? _style.getTextStyle() : firstStyle->textStyle, point);
    }
  }
  point.fY += _height;
}

void ShapedParagraph::GetRectsForRange(const char* start, const char* end, std::vector<SkTextBox>& result) {
  for (auto& line : _lines) {
    for (auto& word : line.words()) {
      if (word.text().end() <= start || word.text().begin() >= end) {
        continue;
      }
      result.emplace_back(word.rect(), SkTextDirection::ltr); // TODO: the right direction
    }
  }
}