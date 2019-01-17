/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkFontArguments.h"
#include "SkLoadICU.h"
#include "SkMalloc.h"
#include "SkOnce.h"
#include "SkFont.h"
#include "SkPoint.h"
#include "SkRefCnt.h"
#include "SkScalar.h"
#include "SkShaper.h"
#include "SkString.h"
#include "SkTArray.h"
#include "SkTDPQueue.h"
#include "SkTemplates.h"
#include "SkTextBlobPriv.h"
#include "SkTo.h"
#include "SkIterators.h"
#include "SkFontPriv.h"

#include <unicode/brkiter.h>
#include <unicode/locid.h>
#include <unicode/stringpiece.h>
#include <unicode/urename.h>
#include <unicode/utext.h>
#include <unicode/utypes.h>

#include <memory>
#include <utility>
#include <cstring>

namespace {

static constexpr bool is_LTR(UBiDiLevel level) {
  return (level & 1) == 0;
}

struct ShapedRunGlyphIterator {
  ShapedRunGlyphIterator(const SkTArray<ShapedRun>& origRuns)
      : fRuns(&origRuns), fRunIndex(0), fGlyphIndex(0)
  { }

  ShapedRunGlyphIterator(const ShapedRunGlyphIterator& that) = default;
  ShapedRunGlyphIterator& operator=(const ShapedRunGlyphIterator& that) = default;
  bool operator==(const ShapedRunGlyphIterator& that) const {
    return fRuns == that.fRuns &&
        fRunIndex == that.fRunIndex &&
        fGlyphIndex == that.fGlyphIndex;
  }
  bool operator!=(const ShapedRunGlyphIterator& that) const {
    return fRuns != that.fRuns ||
        fRunIndex != that.fRunIndex ||
        fGlyphIndex != that.fGlyphIndex;
  }

  ShapedGlyph* next() {
    const SkTArray<ShapedRun>& runs = *fRuns;
    SkASSERT(fRunIndex < runs.count());
    SkASSERT(fGlyphIndex < runs[fRunIndex].fNumGlyphs);

    ++fGlyphIndex;
    if (fGlyphIndex == runs[fRunIndex].fNumGlyphs) {
      fGlyphIndex = 0;
      ++fRunIndex;
      if (fRunIndex >= runs.count()) {
        return nullptr;
      }
    }

    return &runs[fRunIndex].fGlyphs[fGlyphIndex];
  }

  ShapedGlyph* current() {
    const SkTArray<ShapedRun>& runs = *fRuns;
    if (fRunIndex >= runs.count()) {
      return nullptr;
    }
    return &runs[fRunIndex].fGlyphs[fGlyphIndex];
  }

  const SkTArray<ShapedRun>* fRuns;
  int fRunIndex;
  int fGlyphIndex;
};

}  // namespace

SkShaper::SkShaper(const UChar* utf16, size_t utf16Bytes,
                   std::vector<Block>::iterator begin,
                   std::vector<Block>::iterator end,
                   SkTextStyle defaultStyle)
    : fUtf16(utf16)
    , fUtf16Bytes(utf16Bytes) {

  initialize();

  sk_sp<SkFontMgr> fontMgr = SkFontMgr::RefDefault();

  fFontIterator = FontRunIterator::Make(utf16, utf16Bytes, begin, end, defaultStyle);
  FontRunIterator* font = fFontIterator.getMaybeNull();
  if (!font) {
    return;
  }

  fIteratorQueue.insert(font);

}

SkShaper::~SkShaper() {}

bool SkShaper::initialize() {
  SkOnce once;
  once([] { SkLoadICU(); });

  UBiDiLevel defaultLevel = fDefaultStyle.getTextDirection() == SkTextDirection::ltr ? UBIDI_DEFAULT_LTR : UBIDI_DEFAULT_RTL;
  fBidiIterator = BiDiRunIterator::Make(fUtf16, fUtf16Bytes, defaultLevel);
  BiDiRunIterator* bidi = fBidiIterator.getMaybeNull();
  if (!bidi) {
    return false;
  }

  fIteratorQueue.insert(bidi);

  fScriptIterator = ScriptRunIterator::Make(fUtf16, fUtf16Bytes);
  ScriptRunIterator* script = fScriptIterator.getMaybeNull();
  if (!script) {
    return false;
  }
  fIteratorQueue.insert(script);

  icu::Locale thai("th");
  UErrorCode status = U_ZERO_ERROR;
  fBreakIterator.reset(icu::BreakIterator::createLineInstance(thai, status));
  if (U_FAILURE(status)) {
    SkDebugf("Could not create break iterator: %s", u_errorName(status));
    SK_ABORT("");
  }

  return true;
}

bool SkShaper::good() const {
  return fFontIterator.get()->getfHarfBuzzFont() &&
      fScriptIterator.get()->getBuffer() &&
      fFontIterator.get()->currentTypeface() &&
      fBreakIterator;
}

// Simple static method that shapes 8-bytes text
SkPoint SkShaper::shape(SkTextBlobBuilder* builder,
                        const char* utf8,
                        size_t utf8Bytes,
                        const SkFont& font,
                        bool leftToRight,
                        SkPoint point,
                        SkScalar width) {

  icu::UnicodeString utf16 = icu::UnicodeString::fromUTF8(icu::StringPiece(utf8, utf8Bytes));

  std::vector<Block> dummy;
  SkShaper shaper((UChar*) utf16.getBuffer(),  utf16.length(),
                  dummy.begin(), dummy.end(), SkTextStyle());
  if (!shaper.generateGlyphs()) {
    return point;
  }

  // Iterate over the glyphs in logical order to mark line endings.
  shaper.generateLineBreaks(width);

  // Reorder the runs and glyphs per line and write them out.
  return shaper.refineLineBreaks(builder,
                  point,
                  [](const ShapedRun& run, int s, int e, SkPoint point, SkRect background) {},
                  [](bool line_break, size_t line_number, SkSize size, SkScalar spacer, int p, int c) {});
}

bool SkShaper::generateGlyphs() {

  // Find all possible breaks
  icu::BreakIterator& breakIterator = *fBreakIterator;
  {
    UErrorCode status = U_ZERO_ERROR;
    UText utf16UText = UTEXT_INITIALIZER;
    utext_openUChars(&utf16UText, fUtf16, fUtf16Bytes, &status);
    std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> autoClose(&utf16UText);
    if (U_FAILURE(status)) {
      SkDebugf("Could not create utf16UText: %s", u_errorName(status));
      return false;
    }
    breakIterator.setText(&utf16UText, status);
    if (U_FAILURE(status)) {
      SkDebugf("Could not setText on break iterator: %s", u_errorName(status));
      return false;
    }
  }

  ScriptRunIterator* script = fScriptIterator.getMaybeNull();
  BiDiRunIterator* bidi = fBidiIterator.getMaybeNull();
  FontRunIterator* font = fFontIterator.getMaybeNull();

  const UChar* utf16Start = nullptr;
  const UChar* utf16End = fUtf16;
  while (fIteratorQueue.advanceRuns()) {
    utf16Start = utf16End;
    utf16End = fIteratorQueue.endOfCurrentRun();

    hb_buffer_t* buffer = script->getBuffer().get();
    SkAutoTCallVProc<hb_buffer_t, hb_buffer_clear_contents> autoClearBuffer(buffer);
    hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
    hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS);

    // Add precontext.
    hb_buffer_add_utf16(buffer, fUtf16, utf16Start - fUtf16, utf16Start - fUtf16, 0);

    // Populate the hb_buffer directly with utf8 cluster indexes.
    const UChar* utf16Current = utf16Start;
    while (utf16Current < utf16End) {
      unsigned int cluster = utf16Current - utf16Start;
      hb_codepoint_t u = utf16_next(&utf16Current, utf16End);
      hb_buffer_add(buffer, u, cluster);
    }

    // Add postcontext.
    hb_buffer_add_utf16(buffer, utf16Current, fUtf16 + fUtf16Bytes - utf16Current, 0, 0);

    size_t utf8runLength = utf16End - utf16Start;
    if (!SkTFitsIn<int>(utf8runLength)) {
      SkDebugf("Shaping error: utf8 too long");
      return false;
    }
    hb_buffer_set_script(buffer, script->currentScript());
    hb_direction_t direction = is_LTR(bidi->currentLevel()) ? HB_DIRECTION_LTR:HB_DIRECTION_RTL;
    hb_buffer_set_direction(buffer, direction);
    // TODO: language
    hb_buffer_guess_segment_properties(buffer);
    // TODO: features
    if (!font->currentHBFont()) {
      continue;
    }
    hb_shape(font->currentHBFont(), buffer, nullptr, 0);
    unsigned len = hb_buffer_get_length(buffer);
    if (len == 0) {
      continue;
    }

    if (direction == HB_DIRECTION_RTL) {
      // Put the clusters back in logical order.
      // Note that the advances remain ltr.
      hb_buffer_reverse(buffer);
    }
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer, nullptr);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer, nullptr);

    if (!SkTFitsIn<int>(len)) {
      SkDebugf("Shaping error: too many glyphs");
      return false;
    }

    // TODO: Create the font properly
    SkFont srcFont = font->getCurrentFont();
    ShapedRun& run = _runs.emplace_back(utf16Start, utf16End, len, srcFont, bidi->currentLevel(),
                                        std::unique_ptr<ShapedGlyph[]>(new ShapedGlyph[len]));


    hb_codepoint_t space;
    hb_font_get_glyph_from_name (font->currentHBFont(), "space", -1, &space);

    int scaleX, scaleY;
    scaleX = scaleY = 1;
    hb_font_get_scale(font->currentHBFont(), &scaleX, &scaleY);
    double textSizeY = run.fFont.getSize() / scaleY;
    double textSizeX = run.fFont.getSize() / scaleX * run.fFont.getScaleX();
    for (unsigned i = 0; i < len; i++) {
      ShapedGlyph& glyph = run.fGlyphs[i];
      glyph.fID = info[i].codepoint;
      glyph.fCluster = info[i].cluster;
      glyph.fOffset.fX = pos[i].x_offset * textSizeX;
      glyph.fOffset.fY = pos[i].y_offset * textSizeY;
      glyph.fAdvance.fX = pos[i].x_advance * textSizeX;
      glyph.fAdvance.fY = pos[i].y_advance * textSizeY;
      glyph.fMustLineBreakBefore = false;
      glyph.fHasVisual = true; //!font->currentTypeface()->glyphBoundsAreZero(glyph.fID);
      if (glyph.fID == 0) {
        // TODO: how to substitute any control characters with space
        // TODO: better yet, only whitespaces
        glyph.fID = space;
      }
      //info->mask safe_to_break;
    }

    int32_t clusterOffset = utf16Start - fUtf16;
    uint32_t previousCluster = 0xFFFFFFFF;
    for (unsigned i = 0; i < len; ++i) {
      ShapedGlyph& glyph = run.fGlyphs[i];
      int32_t glyphCluster = glyph.fCluster + clusterOffset;
      int32_t breakIteratorCurrent = breakIterator.current();
      while (breakIteratorCurrent != icu::BreakIterator::DONE &&
          breakIteratorCurrent < glyphCluster)
      {
        breakIteratorCurrent = breakIterator.next();
        if (breakIterator.getRuleStatus() == UBRK_LINE_HARD) {
          ShapedGlyph& next = run.fGlyphs[breakIteratorCurrent - clusterOffset];
          next.fMustLineBreakBefore = true;
         }
      }
      glyph.fMayLineBreakBefore = glyph.fCluster != previousCluster && breakIteratorCurrent == glyphCluster;
      previousCluster = glyph.fCluster;

      char glyphname[32];
      hb_font_get_glyph_name (font->currentHBFont(), glyph.fID, glyphname, sizeof(glyphname));
      //SkDebugf ("glyph='%s' %d [%d] %d %s %s\n", glyphname, glyph.fID, i, u_charType(glyph.fID),
      //glyph.fMayLineBreakBefore ? "word" : "",
      //    glyph.fMustLineBreakBefore ? "line" : "");

    }
  }

  return true;
}

bool SkShaper::generateLineBreaks(SkScalar width) {

  bool breakable = false;
  SkScalar widthSoFar = 0;
  bool previousBreakValid = false; // Set when previousBreak is set to a valid candidate break.
  bool canAddBreakNow = false; // Disallow line breaks before the first glyph of a run.
  ShapedRunGlyphIterator previousBreak(this->_runs);
  ShapedRunGlyphIterator glyphIterator(this->_runs);
  while (ShapedGlyph* glyph = glyphIterator.current()) {
    if (glyph->fMustLineBreakBefore) {
      breakable = false;
      widthSoFar = 0;
      previousBreakValid = false;
      canAddBreakNow = false;
      glyphIterator.next();
      continue;
    }
    if (glyph->fMayLineBreakBefore) {
      breakable = true;
    }
    if (canAddBreakNow && glyph->fMayLineBreakBefore) {
      previousBreakValid = true;
      previousBreak = glyphIterator;
    }
    SkScalar glyphWidth = glyph->fAdvance.fX;
    // TODO: if the glyph is non-visible it can be added.
    if (widthSoFar + glyphWidth < width) {
      widthSoFar += glyphWidth;
      glyphIterator.next();
      canAddBreakNow = true;
      continue;
    }

    // TODO: for both of these emergency break cases
    // don't break grapheme clusters and pull in any zero width or non-visible
    if (widthSoFar == 0) {
      // Adding just this glyph is too much, just break with this glyph
      glyphIterator.next();
      previousBreak = glyphIterator;
    } else if (!previousBreakValid) {
      // No break opportunity found yet, just break without this glyph
      previousBreak = glyphIterator;
    }
    glyphIterator = previousBreak;
    glyph = glyphIterator.current();
    if (glyph) {
      glyph->fMustLineBreakBefore = true;
      //auto& run = this->_runs[glyphIterator.fRunIndex];
      //SkDebugf("Break at %d\n", run.fUtf16End - fUtf16);

    }
    widthSoFar = 0;
    previousBreakValid = false;
    canAddBreakNow = false;
  }

  return breakable;
}

void SkShaper::append(SkTextBlobBuilder* builder, const ShapedRun& run, size_t start, size_t end, SkPoint* p) const {

  if (end == start) {
    // TODO: I don't think it should happen, but it does
    return;
  }
  unsigned len = end - start;
  SkPaint tmpPaint;
  run.fFont.LEGACY_applyToPaint(&tmpPaint);
  tmpPaint.setTextEncoding(kGlyphID_SkTextEncoding);

  auto runBuffer = SkTextBlobBuilderPriv::AllocRunTextPos(builder, tmpPaint, len,
                                                          run.fUtf16End - run.fUtf16Start, SkString());
  memcpy(runBuffer.utf8text, run.fUtf16Start, run.fUtf16End - run.fUtf16Start);

  for (unsigned i = 0; i < len; i++) {
    // Glyphs are in logical order, but output ltr since PDF readers seem to expect that.
    const ShapedGlyph& glyph = run.fGlyphs[is_LTR(run.fLevel) ? start + i : end - 1 - i];
    runBuffer.glyphs[i] = glyph.fID;
    runBuffer.clusters[i] = glyph.fCluster;
    reinterpret_cast<SkPoint*>(runBuffer.pos)[i] =
        SkPoint::Make(p->fX + glyph.fOffset.fX, p->fY - glyph.fOffset.fY);
    p->fX += glyph.fAdvance.fX;
    p->fY += glyph.fAdvance.fY;
  }
}

SkPoint SkShaper::refineLineBreaks(SkTextBlobBuilder* builder, const SkPoint& point, RunBreaker runBreaker, LineBreaker lineBreaker) const {

  SkPoint currentPoint = point;
  SkPoint previousPoint = point;

  ShapedRunGlyphIterator previousBreak(this->_runs);
  ShapedRunGlyphIterator glyphIterator(this->_runs);
  SkScalar maxAscent = 0;
  SkScalar maxDescent = 0;
  SkScalar maxLeading = 0;
  int previousRunIndex = -1;
  size_t line_number = 0;
  while (glyphIterator.current()) {

    int runIndex = glyphIterator.fRunIndex;
    int glyphIndex = glyphIterator.fGlyphIndex;
    ShapedGlyph* nextGlyph = glyphIterator.next();

    if (previousRunIndex != runIndex) {
      SkFontMetrics metrics;
      this->_runs[runIndex].fFont.getMetrics(&metrics);
      maxAscent = SkTMin(maxAscent, metrics.fAscent);
      maxDescent = SkTMax(maxDescent, metrics.fDescent);
      maxLeading = SkTMax(maxLeading, metrics.fLeading);
      previousRunIndex = runIndex;
    }

    // Nothing can be written until the baseline is known.
    if (!(nextGlyph == nullptr || nextGlyph->fMustLineBreakBefore)) {
      continue;
    }

    currentPoint.fY -= maxAscent;

    int numRuns = runIndex - previousBreak.fRunIndex + 1;
    SkAutoSTMalloc<4, UBiDiLevel> runLevels(numRuns);
    for (int i = 0; i < numRuns; ++i) {
      runLevels[i] = this->_runs[previousBreak.fRunIndex + i].fLevel;
    }
    SkAutoSTMalloc<4, int32_t> logicalFromVisual(numRuns);
    ubidi_reorderVisual(runLevels, numRuns, logicalFromVisual);

    for (int i = 0; i < numRuns; ++i) {

      int logicalIndex = previousBreak.fRunIndex + logicalFromVisual[i];

      int startGlyphIndex = (logicalIndex == previousBreak.fRunIndex)
                            ? previousBreak.fGlyphIndex
                            : 0;
      int endGlyphIndex = (logicalIndex == runIndex)
                          ? glyphIndex + 1
                          : this->_runs[logicalIndex].fNumGlyphs;

      SkFontMetrics metrics;
      this->_runs[logicalIndex].fFont.getMetrics(&metrics);
      SkScalar runHeight = metrics.fDescent + metrics.fLeading - metrics.fAscent;

      SkPoint backgroundPoint = SkPoint::Make(currentPoint.fX, currentPoint.fY + metrics.fAscent);
      auto startPoint = currentPoint;
      append(builder, this->_runs[logicalIndex], startGlyphIndex, endGlyphIndex, &currentPoint);
      SkScalar runWidth = currentPoint.fX - backgroundPoint.fX;
      SkRect rect = SkRect::MakeXYWH(backgroundPoint.fX, backgroundPoint.fY, runWidth, runHeight);
      runBreaker(this->_runs[logicalIndex], startGlyphIndex, endGlyphIndex, startPoint, rect);
    }

    // Callback to notify about one more line
    ++line_number;
    lineBreaker(
        nextGlyph != nullptr,
        line_number,
        SkSize::Make(currentPoint.fX - point.fX, currentPoint.fY + maxDescent + maxLeading - previousPoint.fY),
        maxDescent + maxLeading,
        previousBreak.fRunIndex,
        runIndex);
    previousPoint = currentPoint;
    currentPoint.fY += maxDescent + maxLeading;
    currentPoint.fX = point.fX;
    maxAscent = 0;
    maxDescent = 0;
    maxLeading = 0;
    previousRunIndex = -1;
    previousBreak = glyphIterator;
  }

  return currentPoint;
}

SkSize SkShaper::breakIntoWords(WordBreaker wordBreaker) const {

  SkTextBlobBuilder builder;
  SkPoint currentPoint = SkPoint::Make(0, 0);
  SkSize size = SkSize::Make(0, 0);

  ShapedRunGlyphIterator previousBreak(this->_runs);
  ShapedRunGlyphIterator glyphIterator(this->_runs);
  SkScalar maxAscent = 0;
  SkScalar maxDescent = 0;
  SkScalar maxLeading = 0;
  int previousRunIndex = -1;
  while (glyphIterator.current()) {
    int runIndex = glyphIterator.fRunIndex;
    int glyphIndex = glyphIterator.fGlyphIndex;
    ShapedGlyph* nextGlyph = glyphIterator.next();

    if (previousRunIndex != runIndex) {
      SkFontMetrics metrics;
      this->_runs[runIndex].fFont.getMetrics(&metrics);
      maxAscent = SkTMin(maxAscent, metrics.fAscent);
      maxDescent = SkTMax(maxDescent, metrics.fDescent);
      maxLeading = SkTMax(maxLeading, metrics.fLeading);
      previousRunIndex = runIndex;
    }

    // Nothing can be written until the baseline is known.
    if (!(nextGlyph == nullptr || nextGlyph->fMayLineBreakBefore)) {
      continue;
    }

    currentPoint.fY -= maxAscent;

    int numRuns = runIndex - previousBreak.fRunIndex + 1;
    SkAutoSTMalloc<4, UBiDiLevel> runLevels(numRuns);
    for (int i = 0; i < numRuns; ++i) {
      runLevels[i] = this->_runs[previousBreak.fRunIndex + i].fLevel;
    }
    SkAutoSTMalloc<4, int32_t> logicalFromVisual(numRuns);
    ubidi_reorderVisual(runLevels, numRuns, logicalFromVisual);

    for (int i = 0; i < numRuns; ++i) {
      int logicalIndex = previousBreak.fRunIndex + logicalFromVisual[i];

      int startGlyphIndex = (logicalIndex == previousBreak.fRunIndex)
                            ? previousBreak.fGlyphIndex
                            : 0;
      int endGlyphIndex = (logicalIndex == runIndex)
                          ? glyphIndex + 1
                          : this->_runs[logicalIndex].fNumGlyphs;
      append(&builder, this->_runs[logicalIndex], startGlyphIndex, endGlyphIndex, &currentPoint);
    }

    // Callback to notify about one more line
    currentPoint.fY += maxDescent + maxLeading;

    wordBreaker(SkSize::Make(currentPoint.fX, currentPoint.fY - size.fHeight),
                previousBreak.fRunIndex,
                runIndex);

    size.fWidth = SkMaxScalar(size.fWidth, currentPoint.fX);
    size.fHeight = currentPoint.fY;

    currentPoint.fX = 0;
    maxAscent = 0;
    maxDescent = 0;
    maxLeading = 0;
    previousRunIndex = -1;
    previousBreak = glyphIterator;
  }

  return size;
}

void SkShaper::resetLayout() {
  _runs.reset();
}

void SkShaper::resetLinebreaks() {

  ShapedRunGlyphIterator glyphIterator(this->_runs);
  while (ShapedGlyph* glyph = glyphIterator.current()) {
    glyph->fMustLineBreakBefore = false;
    glyphIterator.next();
  }
}
