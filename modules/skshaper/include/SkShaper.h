/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkShaper_DEFINED
#define SkShaper_DEFINED

#include <memory>
#include <unicode/ubidi.h>

#include "SkPoint.h"
#include "SkFont.h"
#include "SkTypeface.h"

class SkFont;
class SkTextBlobBuilder;

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

  ShapedRun(const char* utf8Start, const char* utf8End, int numGlyphs, const SkFont& paint,
            UBiDiLevel level, std::unique_ptr<ShapedGlyph[]> glyphs)
      : fUtf8Start(utf8Start), fUtf8End(utf8End), fNumGlyphs(numGlyphs), fPaint(paint)
      , fLevel(level), fGlyphs(std::move(glyphs))
  {

  }

  const char* fUtf8Start;
  const char* fUtf8End;
  int fNumGlyphs;
  SkFont fPaint;
  UBiDiLevel fLevel;
  std::unique_ptr<ShapedGlyph[]> fGlyphs;
};


/**
   Shapes text using HarfBuzz and places the shaped text into a TextBlob.
   If compiled without HarfBuzz, fall back on SkPaint::textToGlyphs.
 */
class SkShaper {
public:
  SkShaper(sk_sp<SkTypeface> face);
  ~SkShaper();

  typedef std::function<void(size_t line_number, SkScalar maxAscent, SkScalar maxDescent, SkScalar maxLeading, int previousRunIndex, int runIndex, SkPoint point)> LineBreaker;

  bool good() const;
    SkPoint shape(SkTextBlobBuilder* dest,
                  const SkFont& srcPaint,
                  const char* utf8text,
                  size_t textBytes,
                  bool leftToRight,
                  SkPoint point,
                  SkScalar width);

  bool generateGlyphs(const SkFont& srcPaint, const char* utf8text, size_t textBytes, bool leftToRight);

  void generateLineBreaks(SkScalar width);

  SkPoint generateTextBlob(SkTextBlobBuilder* builder, const SkPoint& point, LineBreaker lineBreaker = {}) const;

  void append(SkTextBlobBuilder* builder, const ShapedRun& run, int start, int end, SkPoint* p) const;

  void resetLayout();

  void resetLinebreaks();

private:
  SkShaper(const SkShaper&) = delete;
  SkShaper& operator=(const SkShaper&) = delete;

  struct Impl;
  std::unique_ptr<Impl> fImpl;

  SkTArray<ShapedRun> _runs;
};

#endif  // SkShaper_DEFINED
