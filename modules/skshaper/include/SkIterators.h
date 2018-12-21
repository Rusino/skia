/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <unicode/unistr.h>
#include <unicode/ubidi.h>

#include <hb.h>
#include <hb-ot.h>
#include <SkTextStyle.h>

#include "SkTypes.h"
#include "SkTLazy.h"
#include "SkTFitsIn.h"
#include "SkTypeface.h"
#include "SkFontMgr.h"
#include "SkUTF.h"
#include "SkTDPQueue.h"
#include "SkStream.h"
#include "SkTextBlob.h"

template <class T, void(*P)(T*)> using resource = std::unique_ptr<T, SkFunctionWrapper<void, T, P>>;
using HBBlob   = resource<hb_blob_t  , hb_blob_destroy  >;
using HBFace   = resource<hb_face_t  , hb_face_destroy  >;
using HBFont   = resource<hb_font_t  , hb_font_destroy  >;
using HBBuffer = resource<hb_buffer_t, hb_buffer_destroy>;
using ICUBiDi  = resource<UBiDi      , ubidi_close      >;

/** this version replaces invalid utf-8 sequences with code point U+FFFD. */
static inline SkUnichar utf16_next(const UChar** ptr, const UChar* end) {
  SkUnichar val = SkUTF::NextUTF16(ptr, end);
  if (val < 0) {
    return 0xFFFD;  // REPLACEMENT CHARACTER
  }
  return val;
}

HBFont create_hb_font(SkTypeface* tf);

// Comes from the paragraph
struct StyledText {

  StyledText(size_t start, size_t end, SkTextStyle textStyle)
      : start(start), end(end), textStyle(textStyle) { };
  size_t start;
  size_t end;
  SkTextStyle textStyle;
};

class RunIterator {
 public:
  virtual ~RunIterator() {}
  virtual void consume() = 0;
  // Pointer one past the last (utf16) element in the current run.
  virtual const UChar* endOfCurrentRun() const = 0;
  virtual bool atEnd() const = 0;
  bool operator<(const RunIterator& that) const {
    return this->endOfCurrentRun() < that.endOfCurrentRun();
  }
};

class BiDiRunIterator : public RunIterator {
 public:
  static SkTLazy<BiDiRunIterator> Make(const UChar* utf16, size_t utf16Bytes, UBiDiLevel level) {
    SkTLazy<BiDiRunIterator> ret;

    // ubidi only accepts utf16 (though internally it basically works on utf32 chars).
    // We want an ubidi_setPara(UBiDi*, UText*, UBiDiLevel, UBiDiLevel*, UErrorCode*);
    if (!SkTFitsIn<int32_t>(utf16Bytes)) {
      SkDebugf("Bidi error: text too long");
      return ret;
    }

    UErrorCode status = U_ZERO_ERROR;
    ICUBiDi bidi(ubidi_openSized(utf16Bytes, 0, &status));
    if (U_FAILURE(status)) {
      SkDebugf("Bidi error: %s", u_errorName(status));
      return ret;
    }
    SkASSERT(bidi);

    // The required lifetime of utf16 isn't well documented.
    // It appears it isn't used after ubidi_setPara except through ubidi_getText.
    ubidi_setPara(bidi.get(), utf16, utf16Bytes, level, nullptr, &status);
    if (U_FAILURE(status)) {
      SkDebugf("Bidi error: %s", u_errorName(status));
      return ret;
    }

    ret.init(utf16, utf16 + utf16Bytes, std::move(bidi));
    return ret;
  }
  BiDiRunIterator(const UChar* utf16, const UChar* end, ICUBiDi bidi)
      : fBidi(std::move(bidi))
      , fEndOfCurrentRun(utf16)
      , fEndOfAllRuns(end)
      , fUTF16LogicalPosition(0)
      , fLevel(UBIDI_DEFAULT_LTR)
  {}

  void consume() override {
    SkASSERT(fUTF16LogicalPosition < ubidi_getLength(fBidi.get()));
    int32_t endPosition = ubidi_getLength(fBidi.get());
    fLevel = ubidi_getLevelAt(fBidi.get(), fUTF16LogicalPosition);
    SkUnichar u = utf16_next(&fEndOfCurrentRun, fEndOfAllRuns);
    fUTF16LogicalPosition += SkUTF::ToUTF16(u);
    UBiDiLevel level;
    while (fUTF16LogicalPosition < endPosition) {
      level = ubidi_getLevelAt(fBidi.get(), fUTF16LogicalPosition);
      if (level != fLevel) {
        break;
      }
      u = utf16_next(&fEndOfCurrentRun, fEndOfAllRuns);
      fUTF16LogicalPosition += SkUTF::ToUTF16(u);
    }
  }
  const UChar* endOfCurrentRun() const override {
    return fEndOfCurrentRun;
  }
  bool atEnd() const override {
    return fUTF16LogicalPosition == ubidi_getLength(fBidi.get());
  }

  UBiDiLevel currentLevel() const {
    return fLevel;
  }
 private:
  ICUBiDi fBidi;
  const UChar* fEndOfCurrentRun;
  const UChar* fEndOfAllRuns;
  int32_t fUTF16LogicalPosition;
  UBiDiLevel fLevel;
};

class ScriptRunIterator : public RunIterator {
 public:
  static SkTLazy<ScriptRunIterator> Make(const UChar* utf16, size_t utf16Bytes)
  {
    SkTLazy<ScriptRunIterator> ret;
    ret.init(utf16, utf16Bytes);
    return ret;
  }
  ScriptRunIterator(const UChar* utf16, size_t utf16Bytes)
      : fCurrent(utf16), fEnd(fCurrent + utf16Bytes)
      , fCurrentScript(HB_SCRIPT_UNKNOWN)
  {
    fBuffer.reset(hb_buffer_create());
    SkASSERT(fBuffer);

    hb_buffer_t* buffer = fBuffer.get();
    fHBUnicode = hb_buffer_get_unicode_funcs(buffer);
  }

  void consume() override {
    SkASSERT(fCurrent < fEnd);
    SkUnichar u = utf16_next(&fCurrent, fEnd);
    fCurrentScript = hb_unicode_script(fHBUnicode, u);
    while (fCurrent < fEnd) {
      const UChar* prev = fCurrent;
      u = utf16_next(&fCurrent, fEnd);
      const hb_script_t script = hb_unicode_script(fHBUnicode, u);
      if (script != fCurrentScript) {
        if (fCurrentScript == HB_SCRIPT_INHERITED || fCurrentScript == HB_SCRIPT_COMMON) {
          fCurrentScript = script;
        } else if (script == HB_SCRIPT_INHERITED || script == HB_SCRIPT_COMMON) {
          continue;
        } else {
          fCurrent = prev;
          break;
        }
      }
    }
    if (fCurrentScript == HB_SCRIPT_INHERITED) {
      fCurrentScript = HB_SCRIPT_COMMON;
    }
  }
  const UChar* endOfCurrentRun() const override {
    return fCurrent;
  }
  bool atEnd() const override {
    return fCurrent == fEnd;
  }

  HBBuffer& getBuffer() { return fBuffer; }

  hb_script_t currentScript() const {
    return fCurrentScript;
  }
 private:
  const UChar* fCurrent;
  const UChar* fEnd;
  hb_unicode_funcs_t* fHBUnicode;
  hb_script_t fCurrentScript;
  HBBuffer fBuffer;
};
/*
class FontRunIterator1 : public RunIterator {
 public:
  static SkTLazy<FontRunIterator1> Make(const UChar* utf16,
                                       size_t utf16Bytes,
                                       sk_sp<SkTypeface> typeface,
                                       sk_sp<SkFontMgr> fallbackMgr)
  {
    SkTLazy<FontRunIterator1> ret;
    ret.init(utf16, utf16Bytes, std::move(typeface), std::move(fallbackMgr));
    return ret;
  }

  FontRunIterator1(const UChar* utf16,
                  size_t utf16Bytes,
                  sk_sp<SkTypeface> typeface,
                  sk_sp<SkFontMgr> fallbackMgr)
      : fCurrent(utf16)
      , fEnd(fCurrent + utf16Bytes)
      , fFallbackMgr(std::move(fallbackMgr))
      , fTypeface(std::move(typeface))
      , fFallbackHBFont(nullptr)
      , fFallbackTypeface(nullptr)
      , fCurrentTypeface(fTypeface.get())
  {
    fHarfBuzzFont = create_hb_font(fTypeface.get());
    SkASSERT(fHarfBuzzFont);
    fHBFont = fHarfBuzzFont.get();
  }

  void consume() override {
    SkASSERT(fCurrent < fEnd);
    SkUnichar u = utf16_next(&fCurrent, fEnd);
    // If the starting typeface can handle this character, use it.
    if (fTypeface->charsToGlyphs(&u, SkTypeface::kUTF32_Encoding, nullptr, 1)) {
      fCurrentTypeface = fTypeface.get();
      fCurrentHBFont = fHBFont;
      // If the current fallback can handle this character, use it.
    } else if (fFallbackTypeface &&
        fFallbackTypeface->charsToGlyphs(&u, SkTypeface::kUTF32_Encoding, nullptr, 1))
    {
      fCurrentTypeface = fFallbackTypeface.get();
      fCurrentHBFont = fFallbackHBFont.get();
      // If not, try to find a fallback typeface
    } else {
      fFallbackTypeface.reset(fFallbackMgr->matchFamilyStyleCharacter(
          nullptr, fTypeface->fontStyle(), nullptr, 0, u));
      fFallbackHBFont = create_hb_font(fFallbackTypeface.get());
      fCurrentTypeface = fFallbackTypeface.get();
      fCurrentHBFont = fFallbackHBFont.get();
    }

    while (fCurrent < fEnd) {
      const UChar* prev = fCurrent;
      u = utf16_next(&fCurrent, fEnd);

      // If not using initial typeface and initial typeface has this character, stop fallback.
      if (fCurrentTypeface != fTypeface.get() &&
          fTypeface->charsToGlyphs(&u, SkTypeface::kUTF32_Encoding, nullptr, 1))
      {
        fCurrent = prev;
        return;
      }
      // If the current typeface cannot handle this character, stop using it.
      if (!fCurrentTypeface->charsToGlyphs(&u, SkTypeface::kUTF32_Encoding, nullptr, 1)) {
        fCurrent = prev;
        return;
      }
    }
  }
  const UChar* endOfCurrentRun() const override {
    return fCurrent;
  }
  bool atEnd() const override {
    return fCurrent == fEnd;
  }

  HBFont& getfHarfBuzzFont() { return fHarfBuzzFont; }
  sk_sp<SkTypeface> getTypeface() { return fTypeface; }

  SkTypeface* currentTypeface() const {
    return fCurrentTypeface;
  }
  hb_font_t* currentHBFont() const {
    return fCurrentHBFont;
  }

 private:
  const UChar* fCurrent;
  const UChar* fEnd;
  sk_sp<SkFontMgr> fFallbackMgr;
  hb_font_t* fHBFont;
  sk_sp<SkTypeface> fTypeface;
  HBFont fFallbackHBFont;
  sk_sp<SkTypeface> fFallbackTypeface;
  hb_font_t* fCurrentHBFont;
  SkTypeface* fCurrentTypeface;
  HBFont fHarfBuzzFont;
};
*/
class FontRunIterator : public RunIterator {
 public:

  static SkTLazy<FontRunIterator> Make(const UChar* utf16,
                                        size_t utf16Bytes,
                                        std::vector<StyledText>::iterator begin,
                                        std::vector<StyledText>::iterator end,
                                        SkTextStyle defaultStyle)
  {
    SkTLazy<FontRunIterator> ret;
    ret.init(utf16, utf16Bytes, begin, end, defaultStyle);
    return ret;
  }

  FontRunIterator(const UChar* utf16,
                  size_t utf16Bytes,
                  std::vector<StyledText>::iterator begin,
                  std::vector<StyledText>::iterator end,
                  SkTextStyle defaultStyle)
      : fCurrent(utf16)
      , fStart(utf16)
      , fEnd(fCurrent + utf16Bytes)
      , fCurrentStyle(SkTextStyle())
      , fDefaultStyle(defaultStyle)
      , fIterator(begin)
      , fLast(end)
  {
    fTypeface = defaultStyle.getTypeface();
    fHarfBuzzFont = create_hb_font(fTypeface.get());
    fHBFont = fHarfBuzzFont.get();
  }

  void consume() override {

    SkASSERT(fIterator != fLast);

    if (fIterator == fLast) {
      fCurrent = fEnd;
      fCurrentStyle = fDefaultStyle;
    } else {
      fCurrent = fStart + fIterator->end;
      fCurrentStyle = fIterator->textStyle;
    }

    fCurrentTypeface = fCurrentStyle.getTypeface().get();
    fHarfBuzzFont = create_hb_font(fTypeface.get());
    SkASSERT(fHarfBuzzFont);
    fCurrentHBFont = fHarfBuzzFont.get();

    while (fIterator != fLast &&
        SkTypeface::Equal(fCurrentStyle.getTypeface().get(), fIterator->textStyle.getTypeface().get())) {
      ++fIterator;
    }
  }

  const UChar* endOfCurrentRun() const override {
    return fCurrent;
  }
  bool atEnd() const override {
    return fCurrent == fEnd;
  }

  SkTextStyle currentTextStyle() const {
    return fCurrentStyle;
  }

  SkTextStyle currentDefaultStyle() const {
    return fDefaultStyle;
  }

  HBFont& getfHarfBuzzFont() { return fHarfBuzzFont; }
  sk_sp<SkTypeface> getTypeface() { return fTypeface; }

  SkTypeface* currentTypeface() const {
    return fCurrentTypeface;
  }

  SkFont getCurrentFont() {

    SkFont font;
    font.setSize(fCurrentStyle.getFontSize());
    font.setEdging(SkFont::Edging::kAlias);
    font.setTypeface(SkTypeface::MakeFromName(fCurrentStyle.getFontFamily().data(), fCurrentStyle.getFontStyle()));

    return font;
  }

  hb_font_t* currentHBFont() const {
    return fCurrentHBFont;
  }

 private:
  const UChar* fCurrent;
  const UChar* fStart;
  const UChar* fEnd;
  SkTextStyle fCurrentStyle;
  SkTextStyle fDefaultStyle;
  std::vector<StyledText>::iterator fIterator;
  std::vector<StyledText>::iterator fFirst;
  std::vector<StyledText>::iterator fLast;

  HBFont fHarfBuzzFont;
  hb_font_t* fHBFont;
  sk_sp<SkTypeface> fTypeface;

  hb_font_t* fCurrentHBFont;
  SkTypeface* fCurrentTypeface;
};

class RunIteratorQueue {
 public:
  void insert(RunIterator* runIterator) {
    fRunIterators.insert(runIterator);
  }

  bool advanceRuns() {
    const RunIterator* leastRun = fRunIterators.peek();

    if (leastRun->atEnd()) {
      SkASSERT(this->allRunsAreAtEnd());
      return false;
    }
    const UChar* leastEnd = leastRun->endOfCurrentRun();
    RunIterator* currentRun = nullptr;
    SkDEBUGCODE(const UChar* previousEndOfCurrentRun);
    while ((currentRun = fRunIterators.peek())->endOfCurrentRun() <= leastEnd) {
      fRunIterators.pop();
      SkDEBUGCODE(previousEndOfCurrentRun = currentRun->endOfCurrentRun());
      currentRun->consume();
      SkASSERT(previousEndOfCurrentRun < currentRun->endOfCurrentRun());
      fRunIterators.insert(currentRun);
    }
    return true;
  }

  const UChar* endOfCurrentRun() const {
    return fRunIterators.peek()->endOfCurrentRun();
  }

 private:
  bool allRunsAreAtEnd() const {
    for (int i = 0; i < fRunIterators.count(); ++i) {
      if (!fRunIterators.at(i)->atEnd()) {
        return false;
      }
    }
    return true;
  }

  static bool CompareRunIterator(RunIterator* const& a, RunIterator* const& b) {
    return *a < *b;
  }
  SkTDPQueue<RunIterator*, CompareRunIterator> fRunIterators;
};