/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <unicode/brkiter.h>
#include <SkBlurTypes.h>
#include <SkFontMgr.h>
#include <unicode/ubidi.h>
#include "SkSpan.h"
#include "SkParagraphImpl.h"
#include "SkPictureRecorder.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "SkCanvas.h"
#include "SkMaskFilter.h"
#include "SkUTF.h"

namespace {

  std::string toString(SkSpan<const char> text) {
    icu::UnicodeString
        utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
    std::string str;
    utf16.toUTF8String(str);
    return str;
  }

  void print(const std::string& label, const SkCluster* cluster) {
    SkDebugf("%s[%d:%f]: '%s'\n", label.c_str(), cluster->fStart, cluster->fRun->position(cluster->fStart).fX, toString(cluster->fText).c_str());
  }

  SkSpan<const char> operator*(const SkSpan<const char>& a, const SkSpan<const char>& b) {
    auto begin = SkTMax(a.begin(), b.begin());
    auto end = SkTMin(a.end(), b.end());
    return SkSpan<const char>(begin, begin <= end ? end - begin : 0);
  }

  static inline SkUnichar utf8_next(const char** ptr, const char* end) {
    SkUnichar val = SkUTF::NextUTF8(ptr, end);
    if (val < 0) {
      return 0xFFFD;  // REPLACEMENT CHARACTER
    }
    return val;
  }
}

SkParagraph::SkParagraph(const std::u16string& utf16text, SkParagraphStyle style, sk_sp<SkFontCollection> fonts)
    : fFontCollection(std::move(fonts))
    , fParagraphStyle(style) {
  icu::UnicodeString unicode((UChar*) utf16text.data(), SkToS32(utf16text.size()));
  std::string str;
  unicode.toUTF8String(str);
  fUtf8 = SkSpan<const char>(str.data(), str.size());
}

SkParagraphImpl::~SkParagraphImpl() = default;

void SkParagraphImpl::resetContext() {

  fAlphabeticBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fIdeographicBaseline = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
  fMaxLineWidth = 0;

  fPicture = nullptr;
  fRuns.reset();
  fClusters.reset();
  fIndexes.reset();
  fTextWrapper.reset();
}

bool SkParagraphImpl::layout(double doubleWidth) {

  auto width = SkDoubleToScalar(doubleWidth);

  this->resetContext();

  this->shapeTextIntoEndlessLine(fUtf8, SkSpan<SkBlock>(fTextStyles.begin(), fTextStyles.size()));

  this->buildClusterTable();

  this->markClustersWithLineBreaks();

  this->breakShapedTextIntoLines(width);

  // The next call does not do the formatting
  // (it's postponed until/if the actual painting happened)
  // but does correct the paragraph width
  this->formatLinesByText(width);

  return true;
}

// TODO: this is a temp solution until we find the right one
void SkParagraphImpl::rearrangeRunsByBidi(SkLine& line) {

  auto start = findCluster(line.text().begin());
  auto end = findCluster(line.text().end() - 1);
  int32_t numRuns = end->fRun->index() - start->fRun->index() + 1;
  SkAutoSTMalloc<4, UBiDiLevel> runLevels(numRuns);

  int32_t i = 0;
  for (auto run = start->fRun; run <= end->fRun; ++run) {
    runLevels[i++] = run->fBidiLevel;
  }

  SkAutoSTMalloc<4, int32_t> logicalFromVisual(numRuns);
  ubidi_reorderVisual(runLevels, numRuns, logicalFromVisual);

  for (i = 0; i < numRuns; ++i) {
    auto index1 = start->fRun->index() + logicalFromVisual[i];
    auto& run = fRuns[index1];
    SkDebugf("%d %d:", index1, run.fBidiLevel);
    SkSpan<const char> text(fUtf8.begin() + run.range().begin(), run.range().size());
    SkDebugf("%s\n", toString(text).c_str());
    line.addVisual(i, &fRuns[index1]);
  }
}

void SkParagraphImpl::paint(SkCanvas* canvas, double x, double y) {

  if (fRuns.empty()) {
    return;
  }

  if (nullptr == fPicture) {
    // Build the picture lazily not until we actually have to paint (or never)
    this->formatLinesByWords(fWidth);

    int i = 0;
    for (auto& line : fTextWrapper.getLines()) {
      SkDebugf("Line #%d: %s\n", ++i, toString(line.text()).c_str());
      if (line.empty()) continue;
      this->rearrangeRunsByBidi(line);
    }

    SkPictureRecorder recorder;
    SkCanvas* textCanvas = recorder.beginRecording(fWidth, fHeight, nullptr, 0);

    for (auto& line : fTextWrapper.getLines()) {

      if (line.empty()) continue;

      textCanvas->save();
      textCanvas->translate(line.offset().fX, line.offset().fY);

      this->iterateThroughStyles(line, SkStyleType::Background,
         [this, textCanvas, &line](SkSpan<const char> text, SkTextStyle style, bool endsWithEllipsis) {
           this->paintBackground(textCanvas, line, text, style, endsWithEllipsis);
           return true;
         });

      this->iterateThroughStyles(line, SkStyleType::Shadow,
         [this, textCanvas, &line](SkSpan<const char> text, SkTextStyle style,  bool endsWithEllipsis) {
           this->paintShadow(textCanvas, line, text, style, endsWithEllipsis);
           return true;
         });

      this->iterateThroughStyles(line, SkStyleType::Foreground,
         [this, textCanvas, &line](SkSpan<const char> text, SkTextStyle style,  bool endsWithEllipsis) {
           this->paintText(textCanvas, line, text, style, endsWithEllipsis);
           return true;
         });

      this->iterateThroughStyles(line, SkStyleType::Decorations,
         [this, textCanvas, &line](SkSpan<const char> text, SkTextStyle style,  bool endsWithEllipsis) {
           this->paintDecorations(textCanvas, line, text, style, endsWithEllipsis);
           return true;
         });

      textCanvas->restore();
    }

    fPicture = recorder.finishRecordingAsPicture();
  }

  SkMatrix matrix = SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
  canvas->drawPicture(fPicture, &matrix, nullptr);
}

void SkParagraphImpl::paintText(
    SkCanvas* canvas,
    const SkLine& line,
    SkSpan<const char> text,
    const SkTextStyle& style,
    bool endsWithEllipsis) const {

  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.setColor(style.getColor());
  }

  this->iterateThroughRunsInVisualOrder(
      line, text, endsWithEllipsis,
    [paint, canvas, &line](const SkRun* run,
                           int32_t pos,
                           size_t size,
                           SkScalar offsetX,
                           SkRect clip,
                           SkScalar shift) {

      // TODO: take offsetX in account
      SkTextBlobBuilder builder;
      run->copyTo(builder,
                  SkToU32(pos),
                  size,
                  SkVector::Make(offsetX, line.sizes().leading() / 2 - line.sizes().ascent()));
      canvas->save();
      canvas->clipRect(clip);
      canvas->translate(shift, 0);
      canvas->drawTextBlob(builder.make(), 0, 0, paint);
      canvas->restore();
      return true;
    });
}

void SkParagraphImpl::paintBackground(
    SkCanvas* canvas,
    const SkLine& line,
    SkSpan<const char> text,
    const SkTextStyle& style,
    bool endsWithEllipsis) const {

  if (!style.hasBackground()) return;
  this->iterateThroughRunsInVisualOrder(
      line, text, endsWithEllipsis,
      [canvas, style](const SkRun* run,
                      int32_t pos,
                      size_t size,
                      SkScalar offsetX,
                      SkRect clip,
                      SkScalar shift) {
        canvas->drawRect(clip, style.getBackground());
        return true;
      });
}

void SkParagraphImpl::paintShadow(
    SkCanvas* canvas,
    const SkLine& line,
    SkSpan<const char> text,
    const SkTextStyle& style,
    bool endsWithEllipsis) const {

  if (style.getShadowNumber() == 0) return;

  for (SkTextShadow shadow : style.getShadows()) {

    if (!shadow.hasShadow()) continue;

    SkPaint paint;
    paint.setColor(shadow.fColor);
    if (shadow.fBlurRadius != 0.0) {
      auto filter = SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, SkDoubleToScalar(shadow.fBlurRadius), false);
      paint.setMaskFilter(filter);
    }

    this->iterateThroughRunsInVisualOrder(
        line, text, endsWithEllipsis,
      [canvas, shadow, paint, &line](const SkRun* run,
                                     size_t pos,
                                     size_t size,
                                     SkScalar offsetX,
                                     SkRect clip,
                                     SkScalar shift) {
        // TODO: take offsetX in account
        SkTextBlobBuilder builder;
        run->copyTo(builder,
                    pos,
                    size,
                    SkVector::Make(0, line.sizes().leading() / 2 - line.sizes().ascent()));
        canvas->save();
        clip.offset(shadow.fOffset);
        canvas->clipRect(clip);
        canvas->translate(shift, 0);
        canvas->drawTextBlob(builder.make(),
                             shadow.fOffset.x(),
                             shadow.fOffset.y(),
                             paint);
        canvas->restore();
        return true;
      });
  }
}

void SkParagraphImpl::computeDecorationPaint(
    SkPaint& paint,
    SkRect clip,
    const SkTextStyle& style,
    SkPath& path) const {

  paint.setStyle(SkPaint::kStroke_Style);
  if (style.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(style.getColor());
  } else {
    paint.setColor(style.getDecorationColor());
  }

  SkScalar scaleFactor = style.getFontSize() / 14.f;

  switch (style.getDecorationStyle()) {
    case SkTextDecorationStyle::kSolid:
      break;

    case SkTextDecorationStyle::kDouble:
      break;

      // Note: the intervals are scaled by the thickness of the line, so it is
      // possible to change spacing by changing the decoration_thickness
      // property of TextStyle.
    case SkTextDecorationStyle::kDotted: {
      const SkScalar intervals[] =
          {1.0f * scaleFactor, 1.5f * scaleFactor, 1.0f * scaleFactor,
           1.5f * scaleFactor};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, (int32_t) count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
      // Note: the intervals are scaled by the thickness of the line, so it is
      // possible to change spacing by changing the decoration_thickness
      // property of TextStyle.
    case SkTextDecorationStyle::kDashed: {
      const SkScalar intervals[] =
          {4.0f * scaleFactor, 2.0f * scaleFactor, 4.0f * scaleFactor,
           2.0f * scaleFactor};
      size_t count = sizeof(intervals) / sizeof(intervals[0]);
      paint.setPathEffect(SkPathEffect::MakeCompose(
          SkDashPathEffect::Make(intervals, (int32_t) count, 0.0f),
          SkDiscretePathEffect::Make(0, 0)));
      break;
    }
    case SkTextDecorationStyle::kWavy: {

      int wave_count = 0;
      SkScalar x_start = 0;
      SkScalar wavelength = 2 * scaleFactor;
      auto width = clip.width();
      path.moveTo(0, 0);
      while (x_start + wavelength * 2 < width) {
        path.rQuadTo(wavelength,
                     wave_count % 2 != 0 ? wavelength : -wavelength,
                     wavelength * 2,
                     0);
        x_start += wavelength * 2;
        ++wave_count;
      }
      break;
    }
  }
}

void SkParagraphImpl::paintDecorations(
    SkCanvas* canvas,
    const SkLine& line,
    SkSpan<const char> text,
    const SkTextStyle& style,
    bool endsWithEllipsis) const {

  if (style.getDecoration() == SkTextDecoration::kNoDecoration) return;

  iterateThroughRunsInVisualOrder(
      line, text, endsWithEllipsis,
      [this, canvas, style](const SkRun* run,
                            int32_t pos,
                            size_t size,
                            SkScalar offsetX,
                            SkRect clip,
                            SkScalar shift) {

        // TODO: take offsetX in account
        SkScalar thickness = style.getDecorationThicknessMultiplier();
        SkScalar position;
        switch (style.getDecoration()) {
          case SkTextDecoration::kUnderline:
            position = -run->ascent() + thickness;
            break;
          case SkTextDecoration::kOverline:
            position = 0;
            break;
          case SkTextDecoration::kLineThrough: {
            position = (run->descent() - run->ascent() - thickness) / 2;
            break;
          }
          default:
            position = 0;
            SkASSERT(false);
            break;
        }

        auto width = clip.width();
        SkScalar x = clip.left();
        SkScalar y = clip.top() + position;

        // Decoration paint (for now) and/or path
        SkPaint paint;
        SkPath path;
        this->computeDecorationPaint(paint, clip, style, path);
        paint.setStrokeWidth(thickness);

        switch (style.getDecorationStyle()) {
          case SkTextDecorationStyle::kWavy:
            path.offset(x, y);
            canvas->drawPath(path, paint);
            break;
          case SkTextDecorationStyle::kDouble: {
            canvas->drawLine(x, y, x + width, y, paint);
            SkScalar bottom = y + thickness * 2;
            canvas->drawLine(x, bottom, x + width, bottom, paint);
            break;
          }
          case SkTextDecorationStyle::kDashed:
          case SkTextDecorationStyle::kDotted:
          case SkTextDecorationStyle::kSolid:
            canvas->drawLine(x, y, x + width, y, paint);
            break;
          default:
            break;
        }
        return true;
      });
}

void SkParagraphImpl::buildClusterTable() {

  for (auto& run : fRuns) {

    run.iterateThroughClusters(
        [&run, this](size_t glyphStart, size_t glyphEnd, size_t charStart, size_t charEnd, SkVector size){
      SkCluster data;
      data.fRun = &run;
      data.fStart = glyphStart;
      data.fEnd = glyphEnd;
      data.fText = SkSpan<const char>(fUtf8.begin() + charStart, charEnd - charStart);

      data.fWidth = size.fX;
      data.fHeight = size.fY;
      fClusters.emplace_back(data);
      fIndexes.set(fUtf8.begin() + charStart, fClusters.size() - 1);
    });
  }
}

void SkParagraphImpl::shapeTextIntoEndlessLine(SkSpan<const char> text, SkSpan<SkBlock> styles) {

 class MultipleFontRunIterator final : public SkShaper::FontRunIterator {
   public:
    MultipleFontRunIterator(
        SkSpan<const char> utf8,
        SkSpan<SkBlock> styles,
        sk_sp<SkFontCollection> fonts,
        bool hintingOn)
        : fText(utf8)
        , fCurrentChar(utf8.begin())
        , fCurrentStyle(styles.begin())
        , fLast(styles.end())
        , fFontCollection(fonts)
        , fHintingOn(hintingOn) {
    }

    void consume() override {

      auto start = fCurrentChar;
      char ch = *fCurrentChar;
      SkUnichar u = utf8_next(&fCurrentChar, fText.end());
      auto currentStyle = fCurrentStyle->style();

      // Find the font
      for (auto& fontFamily : currentStyle.getFontFamilies()) {
        sk_sp<SkTypeface> typeface = fFontCollection->findTypeface(fontFamily, currentStyle.getFontStyle());
        if (typeface == nullptr) {
          continue;
        }
        // Get the font
        fFont = SkFont(typeface, currentStyle.getFontSize());
        fFont.setEdging(SkFont::Edging::kAntiAlias);
        if (!fHintingOn) {
          fFont.setHinting(SkFontHinting::kSlight);
          fFont.setSubpixel(true);
        }
        fFontFamilyName = fontFamily;
        fFontStyle = currentStyle.getFontStyle();
        if (ignored(ch) || fFont.unicharToGlyph(u)) {
          // If the current font can handle this character, use it
          break;
        }
      }

      // Find the character that cannot be shown in that font
      while (fCurrentChar != fText.end() &&
                currentFontListedInCurrentStyle() &&
                    currentCharExistsInCurrentFont()) {
        // Move the style iterator along with the character
        if (fCurrentChar == fCurrentStyle->text().end()) {
          ++fCurrentStyle;
        }
      }
      if (false) {
        SkSpan<const char> text(start, fCurrentChar - start);
        SkDebugf("%s,%f : '%s'\n", fFontFamilyName.c_str(), fFont.getSize(), toString(text).c_str());
      }
    }

    size_t endOfCurrentRun() const override { return fCurrentChar - fText.begin(); }
    bool atEnd() const override { return fCurrentChar == fText.end(); }
    const SkFont& currentFont() const override { return fFont; }

    bool currentFontListedInCurrentStyle() {

      auto currentStyle = fCurrentStyle->style();
      return currentStyle.getFontStyle() == fFontStyle &&
             currentStyle.getFontSize() == fFont.getSize() &&
             currentStyle.getFontFamilies()[0] == fFontFamilyName;
    }

    bool ignored(char ch) {
      return u_charType(ch) == U_CONTROL_CHAR ||
             u_charType(ch) == U_NON_SPACING_MARK;
    }

    bool currentCharExistsInCurrentFont() {
      if (ignored(*fCurrentChar)) {
        ++fCurrentChar;
        return true;
      }
      SkUnichar u = utf8_next(&fCurrentChar, fText.end());
      return fFont.unicharToGlyph(u) != 0;
    }

   private:
    SkSpan<const char> fText;
    const char* fCurrentChar;
    SkFont fFont;
    std::string fFontFamilyName;
    SkFontStyle fFontStyle;
    SkBlock* fCurrentStyle;
    SkBlock* fLast;
    sk_sp<SkFontCollection> fFontCollection;
    bool fHintingOn;
  };

  class ShapeHandler final : public SkShaper::RunHandler {

   public:
    explicit ShapeHandler(SkParagraphImpl& paragraph)
        : fParagraph(&paragraph)
        , fAdvance(SkVector::Make(0, 0)) {}

    inline SkVector advance() const { return fAdvance; }

   private:

    void beginLine() override { }

    void runInfo(const RunInfo&) override { }

    void commitRunInfo() override { }

    Buffer runBuffer(const RunInfo& info) override {

      auto& run = fParagraph->fRuns.emplace_back(info, fAdvance.fX, fParagraph->fRuns.count());
      return run.newRunBuffer();
    }

    void commitRunBuffer(const RunInfo&) override {
      auto& run = fParagraph->fRuns.back();
      if (run.size() == 0) {
        fParagraph->fRuns.pop_back();
        return;
      }
      // Carve out the line text out of the entire run text
      fAdvance.fX += run.advance().fX;
      fAdvance.fY = SkMaxScalar(fAdvance.fY, run.descent() + run.leading() - run.ascent());
    }

    void commitLine() override { }

    SkParagraphImpl* fParagraph;
    SkVector fAdvance;
  };

  MultipleFontRunIterator font(text, styles, fFontCollection, fParagraphStyle.hintingIsOn());
  ShapeHandler handler(*this);
  std::unique_ptr<SkShaper> shaper = SkShaper::MakeShapeThenWrap();

  auto bidi = SkShaper::MakeIcuBiDiRunIterator(fUtf8.begin(), fUtf8.size(),
                          fParagraphStyle.getTextDirection() == SkTextDirection::ltr ? 2 : 1);
  auto script = SkShaper::MakeHbIcuScriptRunIterator(fUtf8.begin(), fUtf8.size());
  auto lang = SkShaper::MakeStdLanguageRunIterator(fUtf8.begin(), fUtf8.size());

  shaper->shape(text.begin(), text.size(),
               font,
               *bidi,
               *script,
               *lang,
               std::numeric_limits<SkScalar>::max(),
               &handler);

  fMaxIntrinsicWidth = handler.advance().fX;
}

void SkParagraphImpl::markClustersWithLineBreaks() {

  // Find all clusters with line breaks
  SkTextBreaker breaker;
  if (!breaker.initialize(fUtf8, UBRK_LINE)) {
    return;
  }

  size_t currentPos = 0;
  SkTHashMap<const char*, bool> map;
  while (!breaker.eof()) {
    currentPos = breaker.next(currentPos);
    map.set(currentPos +fUtf8.begin(), breaker.status() == UBRK_LINE_HARD);
  }

  for (auto& cluster : fClusters) {
    //SkDebugf("Cluster [%d:%d] %f [%d:%d] '%s'\n",
    //         cluster.fStart, cluster.fEnd, cluster.fWidth,
    //         cluster.fText.begin() - fUtf8.begin(),
    //         cluster.fText.end() - fUtf8.begin(),
    //         toString(cluster.fText).c_str());
    auto found = map.find(cluster.fText.end());
    if (found) {
      cluster.fBreakType = *found || &cluster == &fClusters.back()
                            ? SkCluster::BreakType::HardLineBreak
                            : SkCluster::BreakType::SoftLineBreak;
      cluster.setIsWhiteSpaces();
      //SkDebugf("Line break\n");
    }
  }
}

void SkParagraphImpl::breakShapedTextIntoLines(SkScalar maxWidth) {

  fTextWrapper.formatText(
      SkSpan<SkCluster>(fClusters.begin(), fClusters.size()),
      maxWidth,
      fParagraphStyle.getMaxLines(),
      fParagraphStyle.getEllipsis());
  fHeight = fTextWrapper.height();
  fWidth = fTextWrapper.width();
  fMinIntrinsicWidth = fTextWrapper.intrinsicWidth();
}

void SkParagraphImpl::formatLinesByText(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  if (effectiveAlign == SkTextAlign::justify) {
    fWidth = maxWidth;
  } else {
    fWidth = maxWidth;
  }
}

void SkParagraphImpl::formatLinesByWords(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  for (auto& line : fTextWrapper.getLines()) {

    SkScalar delta = maxWidth - line.fAdvance.fX;
    if (delta <= 0) {
      // Delta can be < 0 if there are extra whitespaces at the end of the line;
      // This is a limitation of a current version
      continue;
    }
    switch (effectiveAlign) {
      case SkTextAlign::left:

        line.fShift = 0;
        break;
      case SkTextAlign::right:

        line.fAdvance.fX = maxWidth;
        line.fShift = delta;
        break;
      case SkTextAlign::center: {

        line.fAdvance.fX = maxWidth;
        line.fShift = delta / 2;
        break;
      }
      case SkTextAlign::justify: {

        if (&line != fTextWrapper.getLastLine()) {
          justifyLine(line, maxWidth);
        } else {
          line.fShift = 0;
        }

        break;
      }
      default:
        break;
    }
  }
}

// TODO: Implement justification correctly, but only if it's needed
void SkParagraphImpl::justifyLine(SkLine& line, SkScalar maxWidth) {

  SkScalar len = 0;
  line.breakLineByWords(UBRK_LINE, [this, &len](SkWord& word) {
    word.fAdvance = this->measureText(word.text());
    word.fShift = len;
    len += word.fAdvance.fX;
    return true;
  });

  auto delta = maxWidth - len;
  auto softLineBreaks = line.fWords.size() - 1;
  if (softLineBreaks == 0) {
    auto word = line.fWords.begin();
    word->expand(delta);
    line.fShift = 0;
    line.fAdvance.fX = maxWidth;
    return;
  }

  SkScalar step = delta / softLineBreaks;
  SkScalar shift = 0;

  SkWord* last = nullptr;
  for (auto& word : line.fWords) {

    if (last != nullptr) {
      --softLineBreaks;
      last->expand(step);
      shift += step;
    }

    last = &word;
    word.shift(shift);
    // Correct all runs and position for all the glyphs in the word
    this->iterateThroughRunsInVisualOrder(line, word.text(), false,
                                          [shift](SkRun* run,
                                                  size_t pos,
                                                  size_t size,
                                                  SkScalar offsetX,
                                                  SkRect clip,
                                                  SkScalar) {
                                            // TODO: take offsetX in account
                                            for (auto i = pos; i < pos + size;
                                                 ++i) {
                                              run->fPositions[i].fX += shift;
                                            }
                                            return true;
                                          });
  }

  line.fShift = 0;
  line.fAdvance.fX = maxWidth;
}

SkCluster* SkParagraphImpl::findCluster(const char* ch) const {

  const char* start = ch;

  while (start >= fUtf8.begin()) {
    auto index = fIndexes.find(start);
    if (index != nullptr) {
      auto& cluster = fClusters[*index];
      SkASSERT(cluster.fText.begin() <= ch && cluster.fText.end() > ch);
      return const_cast<SkCluster*>(&cluster);
    }
    --start;
  }
  return nullptr;
}

SkVector SkParagraphImpl::measureText(SkSpan<const char> text) const {

  SkVector size = SkVector::Make(0, 0);
  if (text.empty()) {
    return size;
  }

  auto start = findCluster(text.begin());
  auto end = findCluster(text.end() - 1);
  for (auto cluster = start; cluster <= end; ++cluster) {

    if (cluster == start) {
      size.fX -= cluster->sizeToChar(text.begin());
    }
    if (cluster == end) {
      size.fX += cluster->sizeFromChar(text.end() - 1);
    } else {
      size.fX += cluster->fWidth;
    }
    size.fY = SkTMax(size.fY, cluster->fHeight);
  }

  return size;
}

void SkParagraphImpl::iterateThroughStyles(
    const SkLine& line,
    SkStyleType styleType,
    std::function<bool(SkSpan<const char> text, const SkTextStyle& style, bool endsWithEllipsis)> apply) const {

  // TODO: Make sure we run through the entire line text
  //  (even if there is no special text style for some of it)
  // Maintain the advanceX for all of them (apply should return it)
  const char* start = nullptr;
  size_t size = 0;
  SkTextStyle prevStyle;
  SkDebugf("iterateThroughStyles: '%s'\n", toString(line.text()).c_str());
  for (auto& textStyle : fTextStyles) {

    if (!(textStyle.text() && line.text())) {
      if (start == nullptr) {
        // We haven't found any good style just yet
        continue;
      } else {
        // We have found all the good styles already
        break;
      }
    }

    auto style = textStyle.style();
    auto begin = SkTMax(textStyle.text().begin(), line.text().begin());
    auto end = SkTMin(textStyle.text().end(), line.text().end());
    auto intersect = SkSpan<const char>(begin, end - begin);

    if (start != nullptr && style.matchOneAttribute(styleType, prevStyle)) {
      size += intersect.size();
      continue;
    } else if (size == 0) {
      // First time only
      prevStyle = style;
      size = intersect.size();
      start = intersect.begin();
      continue;
    }
    // Get all the words that cross this span
    // Generate a text blob
    if (!apply(SkSpan<const char>(start, size), prevStyle, false)) {
      return;
    }
    // Start all over again
    prevStyle = style;
    start = intersect.begin();
    size = intersect.size();
  }

  // The very last style
  if (size > 0) {
    apply(SkSpan<const char>(start, size), prevStyle, line.ellipsis() != nullptr);
  }
}

// TODO: have 2 methods: iterate through runs and iterate through visual runs
// Iterate through all the runs on the line in a visual order
void SkParagraphImpl::iterateThroughRunsInVisualOrder(
    const SkLine& line,
    SkSpan<const char> text,
    bool endsWithEllipsis,
    std::function<bool(SkRun* run,
                       size_t pos,
                       size_t size,
                       SkScalar offsetX,
                       SkRect clip,
                       SkScalar shift)> apply) const {

  auto& visuals = line.visuals();
  SkDebugf("\n\n");
  SkDebugf("iterateThroughRunsInVisualOrder: '%s' %f\n", toString(line.text()).c_str(), line.offset().fX);
  SkDebugf("================================\n");
  SkScalar advanceX = 0;
  for (int32_t index = 0; index < visuals.count(); ++index) {
    auto run = visuals[index];

    auto range = run->range();

    SkSpan<const char> runText(&fUtf8[range.begin()], range.size());
    SkSpan<const char> intersect = text * runText;

    SkDebugf("\n");
    SkDebugf("RUN:       '%s'\n", toString(runText).c_str());
    SkDebugf("TEXT:      '%s'\n", toString(text).c_str());
    SkDebugf("INTERSECT: '%s'\n", toString(intersect).c_str());
    SkDebugf("advanceX: %f\n", advanceX);

    if (intersect.empty()) {
      advanceX = run->advance().fX;
      continue;
    }

    auto start = findCluster(intersect.begin());
    auto end = findCluster(intersect.end() - 1);
    if (!run->leftToRight()) {
      std::swap(start, end);
    }

    print("start", start);
    print("end  ", end);

    auto runStart = findCluster(runText.begin());
    auto runEnd = findCluster(runText.end() - 1);
    if (!run->leftToRight()) {
      std::swap(runStart, runEnd);
    }
    print("runStart", runStart);
    print("runEnd", runEnd);

    if (runStart == start) {
      SkDebugf("***************\n");
    }

    size_t size = 0;
    size_t pos = start->fStart;
    SkRect clip = SkRect::MakeXYWH( 0,
                                    run->sizes().diff(line.sizes()),
                                    0,
                                    run->calculateHeight());
    clip.offset(run->offset());
    for (auto cluster = start; cluster <= end; ++cluster) {

      size += (cluster->fEnd - cluster->fStart);
      if (cluster == start) {
        clip.fLeft = cluster->fRun->position(cluster->fStart).fX;
        clip.fRight = clip.fLeft;
        clip.fLeft += cluster->sizeToChar(text.begin());
      }
      if (cluster == end) {
        clip.fRight += cluster->sizeFromChar(text.end() - 1);
      } else {
        //clip.fRight += cluster->fWidth; (because of justification)
        clip.fRight += cluster->fRun->calculateWidth(cluster->fStart, cluster->fEnd);
      }
    }

    SkDebugf("Clip: %f:%f\n", clip.fLeft, clip.fRight);

    SkScalar offsetX = advanceX - start->fRun->position(start->fStart).fX;
    if (offsetX != 0) {
      SkDebugf("SHOULD SHIFT: %f - %f = %f\n", advanceX, clip.fLeft, offsetX);
      //clip.offset(offsetX, 0);
    }
    if (!apply(run, pos, size, 0, clip, 0)) {
      break;
    }

    advanceX += clip.width();

    // TODO: calculate the ellipse for the last visual run
    if (index == line.visuals().count() - 1) {

      if (line.ellipsis() != nullptr) {
        auto ellipsis = line.ellipsis();
        apply(ellipsis, 0, ellipsis->size(), 0, ellipsis->clip(), ellipsis->offset().fX);
      }
      break;
    }
  };
}


// Returns a vector of bounding boxes that enclose all text between
// start and end glyph indexes, including start and excluding end
std::vector<SkTextBox> SkParagraphImpl::getRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rectHeightStyle,
    RectWidthStyle rectWidthStyle) {

  std::vector<SkTextBox> results;
  // Add empty rectangles representing any newline characters within the range
  SkSpan<const char> text(fUtf8.begin() + start, end - start);
  for (auto& line : fTextWrapper.getLines()) {
    auto intersect = line.fText * text;
    if (intersect.size() == 0) continue;

    auto firstBox = results.size();
    SkRect maxClip = SkRect::MakeXYWH(0, 0, 0, 0);
    iterateThroughRunsInVisualOrder(
        line,
        intersect,
        false,
        [&results, &maxClip, &line](SkRun* run,
                                    size_t pos,
                                    size_t size,
                                    SkScalar offsetX,
                                    SkRect clip,
                                    SkScalar shift) {
          // TODO: take offsetX in account
          clip.offset(line.fShift, 0);
          clip.offset(line.fOffset);
          results.emplace_back(clip,
                               run->leftToRight() ? SkTextDirection::ltr : SkTextDirection::rtl);
          maxClip.join(clip);
          return true;
        });

    if (rectHeightStyle != RectHeightStyle::kTight) {
      // Align all the rectangles
      for (auto i = firstBox; i < results.size(); ++i) {
        auto& rect = results[i].rect;
        if (rectHeightStyle == RectHeightStyle::kMax) {
          rect.fTop = maxClip.top();
          rect.fBottom = maxClip.bottom();

        } else if (rectHeightStyle == RectHeightStyle::kIncludeLineSpacingTop) {
          rect.fTop = line.offset().fY;

        } else if (rectHeightStyle == RectHeightStyle::kIncludeLineSpacingMiddle) {
          rect.fTop = line.offset().fY;
          rect.fBottom = line.offset().fY + line.advance().fY;

        } else if (rectHeightStyle == RectHeightStyle::kIncludeLineSpacingBottom) {
          rect.fBottom = line.offset().fY + line.advance().fY;
        }
      }
    } else {
      // Just leave the boxes the way they are
    }

    if (rectWidthStyle == RectWidthStyle::kMax) {
      for (auto i = firstBox; i < results.size(); ++i) {
        auto clip = results[i].rect;
        auto dir = results[i].direction;
        if (clip.fLeft > maxClip.fLeft) {
          SkRect left = SkRect::MakeXYWH(0, clip.fTop, clip.fLeft - maxClip.fLeft, clip.fBottom);
          results.emplace_back(left, dir);
        }
        if (clip.fRight < maxClip.fRight) {
          SkRect right = SkRect::MakeXYWH(0, clip.fTop, maxClip.fRight - clip.fRight, clip.fBottom);
          results.emplace_back(right, dir);
        }
      }
    }
  }

  return results;
}

SkPositionWithAffinity SkParagraphImpl::getGlyphPositionAtCoordinate(double dx, double dy) {
  SkPositionWithAffinity result(0, Affinity::DOWNSTREAM);
  for (auto& line : fTextWrapper.getLines()) {
    if (line.fOffset.fY <= dy && dy < line.fOffset.fY + line.fAdvance.fY) {
      // Find the line
      this->iterateThroughRunsInVisualOrder(
          line,
          line.text(),
          false,
          [dx, &result](const SkRun* run,
                        size_t pos,
                        size_t size,
                        SkScalar offsetX,
                        SkRect clip,
                        SkScalar shift) {
            // TODO: take offsetX in account
            auto offset = run->offset();
            auto advance = run->advance();
            if (offset.fX <= dx && dx < offset.fX + advance.fX) {
              // Find the run
              size_t pos = 0;
              for (size_t i = 0; i < run->size(); ++i) {
                if (run->position(i).fX < dx) {
                  // Find the position
                  pos = i;
                }
              }
              if (pos == 0) {
                result = {SkToS32(run->fClusters[0]), DOWNSTREAM};
              } else if (pos == run->size() - 1) {
                result = {SkToS32(run->fClusters.back()), UPSTREAM};
              } else {
                auto center =
                    (run->position(pos + 1).fX + run->position(pos).fX) / 2;
                if ((dx <= center) == run->leftToRight()) {
                  result = {SkToS32(run->fClusters[pos]), DOWNSTREAM};
                } else {
                  result = {SkToS32(run->fClusters[pos + 1]), UPSTREAM};
                }
              }
              return false;
            }
            return true;
          });
    }
  }
  return result;
}

// Finds the first and last glyphs that define a word containing
// the glyph at index offset.
// By "glyph" they mean a character index - indicated by Minikin's code
SkRange<size_t> SkParagraphImpl::getWordBoundary(unsigned offset) {

  SkTextBreaker breaker;
  if (!breaker.initialize(fUtf8, UBRK_WORD)) {
    return {0, 0};
  }

  size_t currentPos = 0;
  while (true) {
    auto start = currentPos;
    currentPos = breaker.next(currentPos);
    if (breaker.eof()) {
      break;
    }
    if (start <= offset && currentPos > offset) {
      return {start, currentPos};
    }
  }
  return {0, 0};
}
