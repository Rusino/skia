/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "uchar.h"
#include "SkColor.h"
#include "SkCanvas.h"
#include "SkShaper.h"
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"
#include "SkTextBlobPriv.h"

// The smallest part of the text that is painted separately
struct Word {
  Word(const SkFont& font,
       const SkShaper::RunHandler::RunInfo& info,
       int glyphCount,
       const char* start,
       const char* end)
      : fFont(font)
      , fInfo(info)
      , fGlyphs   (glyphCount)
      , fPositions(glyphCount)
      , start(start)
      , end(end)
      , shift(0)
  {
    fGlyphs   .push_back_n(glyphCount);
    fPositions.push_back_n(glyphCount);
  }

  size_t size() const {
    SkASSERT(fGlyphs.size() == fPositions.size());
    return fGlyphs.size();
  }

  SkFont fFont;
  SkShaper::RunHandler::RunInfo   fInfo;
  SkSTArray<128, SkGlyphID, true> fGlyphs;
  SkSTArray<128, SkPoint  , true> fPositions;

  const char* start;
  const char*  end;
  SkTextStyle textStyle;
  sk_sp<SkTextBlob> blob;
  SkRect rect;
  SkScalar shift;
};

// Comes from the paragraph
struct StyledText {

  StyledText() { }
  StyledText(const char* start, const char* end, SkTextStyle textStyle)
      : start(start), end(end), textStyle(textStyle) { }

  bool operator==(const StyledText& rhs) const {
    return start == rhs.start &&
        end == rhs.end &&
        textStyle == rhs.textStyle;
  }
  const char* start;
  const char* end;
  SkTextStyle textStyle;
};

struct Line {
  Line(SkTArray<Word> words, SkVector advance)
      : words(std::move(words)) {
    size.fHeight = advance.fY;
    size.fWidth = advance.fX;
  }
  SkTArray<Word> words;
  SkSize size;
  size_t Length() const { return words.empty() ? 0 :  words.back().end - words.front().start; }
  bool IsEmpty() const { return words.empty() || Length() == 0; }
};

class MultipleFontRunIterator final : public FontRunIterator {
 public:
  MultipleFontRunIterator(const char* utf8,
                          size_t utf8Bytes,
                          std::vector<StyledText>::iterator begin,
                          std::vector<StyledText>::iterator end,
                          SkTextStyle defaultStyle)
      : fCurrent(utf8)
      , fEnd(fCurrent + utf8Bytes)
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
      fCurrent = fNext == fLast ? fEnd : std::next(fCurrent, fNext->start - fIterator->start);
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

class ShapedParagraph final : public SkShaper::RunHandler {
 public:

  ~ShapedParagraph() { }

  ShapedParagraph(SkTextBlobBuilder* builder, SkParagraphStyle style, std::vector<StyledText> styles);

  SkShaper::RunHandler::Buffer newRunBuffer(const RunInfo& info, const SkFont& font, int glyphCount, int textCount) override {
    auto& word = _words.emplace_back(font, info, glyphCount, _currentChar, _currentChar + textCount);

    _maxAscend = SkMinScalar(_maxAscend, word.fInfo.fAscent);
    _maxDescend = SkMaxScalar(_maxDescend, word.fInfo.fDescent);
    _maxLeading = SkMaxScalar(_maxLeading, word.fInfo.fLeading);
    _currentChar = _currentChar + textCount;
    return {
        word.fGlyphs   .data(),
        word.fPositions.data(),
        nullptr,
        nullptr
    };
  }

  void commitLine() override
  {
    SkScalar height = _maxDescend- _maxLeading - _maxAscend;
    _advance.fX = 0;
    for (auto& word : _words) {
      const auto wordSize = word.size();
      const auto& blobBuffer = _builder->allocRunPos(word.fFont, SkToInt(wordSize));

      sk_careful_memcpy(blobBuffer.glyphs,
                        word.fGlyphs.data(),
                        wordSize * sizeof(SkGlyphID));

      const auto offset = SkVector::Make(0, _advance.fY + word.fInfo.fAscent);
      for (size_t i = 0; i < wordSize; ++i) {
        blobBuffer.points()[i] = word.fPositions[SkToInt(i)] + offset;
      }

      word.blob = _builder->make();
      word.rect = SkRect::MakeLTRB(
          _advance.fX,
          _advance.fY,
          _advance.fX + word.fInfo.fAdvance.fX,
          _advance.fY + word.fInfo.fDescent + word.fInfo.fLeading - word.fInfo.fAscent);
      _advance.fX += word.fInfo.fAdvance.fX;

      _maxIntrinsicWidth = SkMaxScalar(_maxIntrinsicWidth, _advance.fX);
      _minIntrinsicWidth = SkMaxScalar(_minIntrinsicWidth, word.fInfo.fAdvance.fX);
    }

    _advance.fY += height;
    _lines.emplace_back(std::move(_words), _advance);
    _height = _advance.fY;
    _width = SkMaxScalar(_width, _advance.fX);
  }

  size_t lineNumber() const { return _lines.size(); }

  void layout(SkScalar maxWidth, size_t maxLines);

  void printBlocks(size_t linenum);

  void format();

  void paint(SkCanvas* textCanvas, SkPoint& point);

  SkScalar alphabeticBaseline() { return _alphabeticBaseline; }
  SkScalar height() { return _height; }
  SkScalar width() { return _width; }
  SkScalar ideographicBaseline() { return _ideographicBaseline; }
  SkScalar maxIntrinsicWidth() { return _maxIntrinsicWidth; }
  SkScalar minIntrinsicWidth() { return _minIntrinsicWidth; }

  void PaintBackground(SkCanvas* canvas, Word word, SkPoint offset) {

    if (!word.textStyle.hasBackground()) {
      return;
    }
    word.rect.offset(offset.fY, offset.fY);
    canvas->drawRect(word.rect, word.textStyle.getBackground());
  }

  void PaintShadow(SkCanvas* canvas, Word word, SkPoint offset) {
    if (word.textStyle.getShadowNumber() == 0) {
      return;
    }

    for (SkTextShadow shadow : word.textStyle.getShadows()) {
      if (!shadow.hasShadow()) {
        continue;
      }

      SkPaint paint;
      paint.setColor(shadow.color);
      if (shadow.blur_radius != 0.0) {
        paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, shadow.blur_radius, false));
      }
      canvas->drawTextBlob(word.blob, offset.x() + shadow.offset.x(), offset.y() + shadow.offset.y(), paint);
    }
  }

  SkScalar ComputeDecorationThickness(SkTextStyle textStyle);

  SkScalar ComputeDecorationPosition(Word word, SkScalar thickness);

  void ComputeDecorationPaint(Word word, SkPaint& paint, SkPath& path, SkScalar width);

  void PaintDecorations(SkCanvas* canvas, Word word, SkPoint offset,  SkScalar width);


  const char* start() { return _styles.front().start; }

  const char* end() { return _styles.back().end; }

  void GetRectsForRange(const char* start, const char* end, std::vector<SkTextBox>& result);

 private:
  // Constrains
  SkScalar _maxWidth;
  size_t _maxLines;

  // Input
  SkParagraphStyle _style;
  std::vector<StyledText> _styles;

  // Output to Flutter
  size_t _linesNumber;
  SkScalar _alphabeticBaseline;
  SkScalar _ideographicBaseline;
  SkScalar _height;
  SkScalar _width;
  SkScalar _maxIntrinsicWidth;
  SkScalar _minIntrinsicWidth;

  // Internal structures
  SkVector _advance;
  SkScalar _maxAscend;
  SkScalar _maxDescend;
  SkScalar _maxLeading;
  bool     _exceededLimits;   // Lines number exceed the limit and there is an ellipse
  SkTArray<Line> _lines;   // All lines that the shaper produced
  SkTArray<Word> _words;   // All words that were shaped on the curret line
  SkTextBlobBuilder* _builder; // One builder for the entire line
  const char* _currentChar;
  // TODO: Shadows
};