/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <memory>
#include <unicode/ubidi.h>

#include "SkPoint.h"
#include "SkFont.h"
#include "SkTypeface.h"
#include "SkUTF.h"
#include "SkTextStyle.h"
#include "SkParagraphStyle.h"
#include "SkPicture.h"
#include "SkIterators.h"

class SkFont;
class SkTextBlobBuilder;
class SkTextBlob;

struct ShapedGlyph {
  ShapedGlyph() { }
  SkGlyphID fID;
  uint32_t fCluster;
  SkPoint fOffset;
  SkVector fAdvance;
  bool fMayLineBreakBefore;
  bool fMustLineBreakBefore;
  bool fHasVisual;
};

struct ShapedRun {

  ShapedRun(const UChar* utf16Start, const UChar* utf16End, int numGlyphs, const SkFont& font,
            UBiDiLevel level, std::unique_ptr<ShapedGlyph[]> glyphs)
      : fUtf16Start(utf16Start), fUtf16End(utf16End), fNumGlyphs(numGlyphs), fFont(font)
      , fLevel(level), fGlyphs(std::move(glyphs))
  {

  }

  const UChar* fUtf16Start;
  const UChar* fUtf16End;
  int fNumGlyphs;
  SkFont fFont;
  UBiDiLevel fLevel;
  std::unique_ptr<ShapedGlyph[]> fGlyphs;
};

/**
   Shapes text using HarfBuzz and places the shaped text into a TextBlob.
   If compiled without HarfBuzz, fall back on SkPaint::textToGlyphs.
 */
class SkShaper {
public:
  SkShaper(const UChar* utf16, size_t utf16Bytes,
           std::vector<Block>::iterator begin,
           std::vector<Block>::iterator end,
           SkTextStyle defaultStyle);
  ~SkShaper();

  typedef std::function<void(bool lineBreak, size_t line_number, SkSize size, SkScalar spacer, int startIndex, int nextStartIndex)> LineBreaker;
  typedef std::function<void(SkSize size, int startIndex, int nextStartIndex)> WordBreaker;
  typedef std::function<void(const ShapedRun& run, int start, int end, SkPoint point, SkRect background)> RunBreaker;

  bool good() const;
  static SkPoint shape(SkTextBlobBuilder* builder,
                       const char* utf8,
                       size_t utf8Bytes,
                       const SkFont& srcPaint,
                       bool leftToRight,
                       SkPoint point,
                       SkScalar width);

  bool generateGlyphs();

  bool generateLineBreaks(SkScalar width);

  SkPoint refineLineBreaks(SkTextBlobBuilder* builder, const SkPoint& point, RunBreaker runBreaker = {}, LineBreaker lineBreaker = {}) const;

  SkSize breakIntoWords(WordBreaker wordBreaker = {}) const;

  void append(SkTextBlobBuilder* builder, const ShapedRun& run, size_t start, size_t end, SkPoint* p) const;

 protected:

  bool initialize();

  void resetLayout();

  void resetLinebreaks();

private:
  SkShaper(const SkShaper&) = delete;
  SkShaper& operator=(const SkShaper&) = delete;

  const UChar* fUtf16;
  size_t fUtf16Bytes;

  SkTArray<ShapedRun> _runs;
  SkParagraphStyle fDefaultStyle;

  SkTLazy<BiDiRunIterator> fBidiIterator;
  SkTLazy<ScriptRunIterator> fScriptIterator;
  SkTLazy<FontRunIterator> fFontIterator;
  std::unique_ptr<icu::BreakIterator> fBreakIterator;
  RunIteratorQueue fIteratorQueue;
};

