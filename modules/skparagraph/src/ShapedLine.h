/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "ShapedRun.h"

class Line {

 public:
  Line()
  {
    fAdvance = SkVector::Make(0, 0);
    maxAscend = 0;
    maxDescend = 0;
    maxLeading = 0;
  }

  void update()
  {
    auto& word = fRuns.back();

    fAdvance.fX += word.advance().fX;

    maxAscend = SkMinScalar(maxAscend, word.ascent());
    maxDescend = SkMaxScalar(maxDescend, word.descent());
    maxLeading = SkMaxScalar(maxLeading, word.leading());
  }

  void finish()
  {
    fAdvance.fY += (maxDescend -maxLeading - maxAscend);
  }

  ShapedRun& addWord(const SkFont& font,
                     const SkShaper::RunHandler::RunInfo& info,
                     int glyphCount,
                     SkSpan<const char> text)
  {
    return fRuns.emplace_back(font, info, glyphCount, text);
  }

  inline ShapedRun& lastWord() { return fRuns.back(); }

  inline SkTArray<ShapedRun>& words() { return fRuns; }

  inline SkVector& advance() { return fAdvance; }

 private:
  SkTArray<ShapedRun> fRuns;
  SkVector fAdvance;
  SkScalar maxAscend;
  SkScalar maxDescend;
  SkScalar maxLeading;
};