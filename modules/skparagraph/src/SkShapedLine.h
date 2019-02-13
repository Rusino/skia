/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include "SkShapedRun.h"

class SkShapedLine {
  public:
    SkShapedLine() {

        fAdvance = SkVector::Make(0, 0);
    }

    void update() {

        auto& word = fRuns.back();

        fAdvance.fX += word.advance().fX;
    }

    void finish() {

        if (!fRuns.empty()) {
            auto& run = fRuns.front();
            fAdvance.fY = (run.descent() + run.leading() - run.ascent());
        }
    }

    SkShapedRun& addWord(const SkFont& font,
        const SkShaper::RunHandler::RunInfo& info,
        int glyphCount,
        SkSpan<const char> text) {

        return fRuns.emplace_back(font, info, glyphCount, text);
    }

    inline SkShapedRun& lastWord() { return fRuns.back(); }

    inline SkTArray<SkShapedRun>& words() { return fRuns; }

    inline SkVector& advance() { return fAdvance; }

  private:
    SkTArray<SkShapedRun> fRuns;
    SkVector fAdvance;
};