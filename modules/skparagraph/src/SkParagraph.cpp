/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <unicode/brkiter.h>
#include "SkSpan.h"
#include "SkParagraph.h"
#include "SkPictureRecorder.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"

std::string toString(SkSpan<const char> text) {
  icu::UnicodeString utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
  std::string str;
  utf16.toUTF8String(str);
  return str;
}

SkParagraph::SkParagraph(const std::string& text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : fParagraphStyle(style)
    , fUtf8(text.data(), text.size()),
      fPicture(nullptr) {
  fTextStyles.reserve(blocks.size());
  for (auto& block : blocks) {
    fTextStyles.emplace_back(SkSpan<const char>(fUtf8.begin() + block.fStart, block.fEnd - block.fStart),
                             block.fStyle);
  }
}

SkParagraph::SkParagraph(const std::u16string& utf16text,
                         SkParagraphStyle style,
                         std::vector<Block> blocks)
    : fParagraphStyle(style)
    , fPicture(nullptr) {

  icu::UnicodeString
      unicode((UChar*) utf16text.data(), SkToS32(utf16text.size()));
  std::string str;
  unicode.toUTF8String(str);
  fUtf8 = SkSpan<const char>(str.data(), str.size());

  fTextStyles.reserve(blocks.size());
  for (auto& block : blocks) {
    fTextStyles.emplace_back(SkSpan<const char>(fUtf8.begin() + block.fStart, block.fEnd - block.fStart),
                             block.fStyle);
  }
}

SkParagraph::~SkParagraph() = default;

void SkParagraph::resetContext() {
  
  fAlphabeticBaseline = 0;
  fHeight = 0;
  fWidth = 0;
  fIdeographicBaseline = 0;
  fMaxIntrinsicWidth = 0;
  fMinIntrinsicWidth = 0;
  fLinesNumber = 0;
  fMaxLineWidth = 0;

  fPicture = nullptr;
  fLines.reset();
  fRuns.reset();
  fClusters.reset();
}

bool SkParagraph::layout(double doubleWidth) {
  
  this->resetContext();

  this->shapeTextIntoEndlessLine();

  this->markClustersWithLineBreaks();

  auto width = SkDoubleToScalar(doubleWidth);

  shapeIntoLines(width, this->linesLeft());

  formatLinesByWords(SkDoubleToScalar(doubleWidth));

  return true;
}

void SkParagraph::paint(SkCanvas* canvas, double x, double y) {

  if (nullptr == fPicture) {
    // Build the picture lazily not until we actually have to paint (or never)
    SkPictureRecorder recorder;
    SkCanvas* textCanvas = recorder.beginRecording(fWidth, fHeight, nullptr, 0);

    for (auto& line : fLines) {

      if (line.empty()) {
        continue;
      }

      auto lineOffset = line.offset();
      textCanvas->save();
      textCanvas->translate(lineOffset.fX, lineOffset.fY);

      iterateThroughStyles(line.text(), SkStyleType::Background,
                           [this, textCanvas](SkSpan<const char> text,
                                              SkTextStyle style) {
                             this->paintBackground(textCanvas, text, style);
                           });

      iterateThroughStyles(line.text(), SkStyleType::Shadow,
                           [this, textCanvas](SkSpan<const char> text,
                                              SkTextStyle style) {
                             this->paintShadow(textCanvas, text, style);
                           });

      iterateThroughStyles(line.text(), SkStyleType::Foreground,
                           [this, textCanvas](SkSpan<const char> text,
                                              SkTextStyle style) {
                             this->paintText(textCanvas, text, style);
                           });

      iterateThroughStyles(line.text(), SkStyleType::Decorations,
                           [this, textCanvas](SkSpan<const char> text,
                                              SkTextStyle style) {
                             this->paintDecorations(textCanvas, text, style);
                           });

      textCanvas->restore();
    }

    fPicture = recorder.finishRecordingAsPicture();
  }

  SkMatrix matrix = SkMatrix::MakeTrans(SkDoubleToScalar(x), SkDoubleToScalar(y));
  canvas->drawPicture(fPicture, &matrix, nullptr);
}

void SkParagraph::paintText(
    SkCanvas* canvas,
    SkSpan<const char> text,
    SkTextStyle style) const {

  // Paint the blob with one foreground color
  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.reset();
    paint.setColor(style.getColor());
  }
  paint.setAntiAlias(true);

  // Build the blob from all the runs
  iterateThroughRuns(text,
                     [canvas, paint](const SkRun* run, int32_t pos, size_t size, SkRect rect) {

                       SkTextBlobBuilder builder;
                       run->copyTo(builder, pos, size);

                       canvas->save();
                       //canvas->clipRect(rect);
                       canvas->drawTextBlob(builder.make(), 0, 0, paint);
                       canvas->restore();
                     });
}

void SkParagraph::paintBackground(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const {

  if (!style.hasBackground()) {
    return;
  }

  iterateThroughRuns(
      text,
      [canvas, style](const SkRun* run, int32_t pos, size_t size, SkRect clip) {
        canvas->drawRect(clip, style.getBackground());
      });
}

void SkParagraph::paintShadow(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle style) const {

  if (style.getShadowNumber() == 0) {
    return;
  }

  for (SkTextShadow shadow : style.getShadows()) {
    if (!shadow.hasShadow()) {
      continue;
    }

    SkPaint paint;
    paint.setColor(shadow.fColor);
    if (shadow.fBlurRadius != 0.0) {
      paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle,
                                                 shadow.fBlurRadius,
                                                 false));
    }

    iterateThroughRuns(text,
                       [canvas, shadow, paint](const SkRun* run, int32_t pos, size_t size, SkRect rect) {

                         SkTextBlobBuilder builder;
                         run->copyTo(builder, pos, size);

                         canvas->save();
                         canvas->clipRect(rect.makeOffset(shadow.fOffset.x(), shadow.fOffset.y()));
                         canvas->drawTextBlob(builder.make(), shadow.fOffset.x(), shadow.fOffset.y(), paint);
                         canvas->restore();
                       });
  }
}

void SkParagraph::computeDecorationPaint(SkPaint& paint, SkRect clip, SkTextStyle textStyle, SkPath& path) const {

  paint.setStyle(SkPaint::kStroke_Style);
  if (textStyle.getDecorationColor() == SK_ColorTRANSPARENT) {
    paint.setColor(textStyle.getColor());
  } else {
    paint.setColor(textStyle.getDecorationColor());
  }
  paint.setAntiAlias(true);

  SkScalar scaleFactor = textStyle.getFontSize() / 14.f;

  switch (textStyle.getDecorationStyle()) {
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

// TODO: Make the thickness reasonable
void SkParagraph::paintDecorations(SkCanvas* canvas, SkSpan<const char> text, SkTextStyle textStyle) const {

  if (textStyle.getDecoration() == SkTextDecoration::kNone) {
    return;
  }

  iterateThroughRuns(text,
                     [this, canvas, textStyle](const SkRun* run, int32_t pos, size_t size, SkRect clip) {

                       SkScalar thickness = textStyle.getDecorationThicknessMultiplier();
                       SkScalar position;
                       switch (textStyle.getDecoration()) {
                         case SkTextDecoration::kUnderline:
                           position = - run->ascent() + thickness;
                           break;
                         case SkTextDecoration::kOverline:
                           position = thickness;
                           break;
                         case SkTextDecoration::kLineThrough: {
                           position = (- run->ascent() - thickness) / 2;
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
                       this->computeDecorationPaint(paint, clip, textStyle, path);
                       paint.setStrokeWidth(thickness);

                       switch (textStyle.getDecorationStyle()) {
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
                           canvas->drawLine(x,
                                            y,
                                            x + width,
                                            y,
                                            paint);
                           break;
                         default:
                           break;
                       }
                     });
}

void SkParagraph::buildClusterTable() {

  // Extrace all the information from SkShaper
  for (auto& run : fRuns) {
    size_t cluster = 0;
    SkScalar width = 0;
    size_t start = 0;
    SkDebugf("Cluster: %d\n", run.size());
    for (size_t pos = 0; pos <= run.size(); ++pos) {

      auto next = pos == run.size() ? run.text().size() : run.cluster(pos);
      width += run.calculateWidth(start, pos);
      if (cluster == next) {
        // Many glyphs in one cluster
        continue;
      } else if (next > cluster + 1) {
        // Many characters in one cluster
      }
      SkCluster data;
      data.fRun = &run;
      data.fStart = start;
      data.fEnd = pos;
      data.fText = SkSpan<const char>(run.text().begin() + cluster, next - cluster);
      data.fWidth = width;
      data.fHeight = run.calculateHeight();
      fClusters.emplace_back(data);

      cluster = next;
      start = pos;
      width = 0;
    }
  }
}

void SkParagraph::shapeTextIntoEndlessLine() {

  class MultipleFontRunIterator final : public FontRunIterator {
   public:
    MultipleFontRunIterator(
        SkSpan<const char> utf8,
        SkSpan<SkBlock> styles)
        : fText(utf8), fCurrent(utf8.begin()), fEnd(utf8.end()),
          fCurrentStyle(SkTextStyle()), fIterator(styles.begin()),
          fNext(styles.begin()), fLast(styles.end()) {

      fCurrentTypeface = SkTypeface::MakeDefault();
      MoveToNext();
    }

    void consume() override {

      if (fIterator == fLast) {
        fCurrent = fEnd;
      } else {
        fCurrent = fNext == fLast ? fEnd : std::next(fCurrent,
                                                     fNext->text().begin() - fIterator->text().begin());
        fCurrentStyle = fIterator->style();
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
      auto nextTypeface = fNext->style().getTypeface();
      auto nextFontSize = fNext->style().getFontSize();
      auto nextFontStyle = fNext->style().getFontStyle();
      while (fNext != fLast
          && fNext->style().getTypeface() == nextTypeface
          && nextFontSize == fNext->style().getFontSize()
          && nextFontStyle == fNext->style().getFontStyle()) {
        ++fNext;
      }
    }

   private:
    SkSpan<const char> fText;
    const char* fCurrent;
    const char* fEnd;
    SkFont fFont;
    SkTextStyle fCurrentStyle;
    SkBlock* fIterator;
    SkBlock* fNext;
    SkBlock* fLast;
    sk_sp<SkTypeface> fCurrentTypeface;
  };

  class ShapeHandler final : public SkShaper::RunHandler {

   public:
    explicit ShapeHandler(SkParagraph& paragraph)
        : fParagraph(&paragraph)
        , fAdvance(SkVector::Make(0, 0)) {}

    inline SkVector advance() const { return fAdvance; }

   private:
    // SkShaper::RunHandler interface
    SkShaper::RunHandler::Buffer newRunBuffer(
        const RunInfo& info,
        const SkFont& font,
        int glyphCount,
        SkSpan<const char> utf8) override {
      // Runs always go to the end of the list even if we insert words in the middle
      auto& run = fParagraph->fRuns.emplace_back(fParagraph->fRuns.size(), font, info, glyphCount, utf8);
      return run.newRunBuffer();
    }

    void commitRun() override {

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

    SkParagraph* fParagraph;
    SkVector fAdvance;
  };

  MultipleFontRunIterator font(fUtf8, SkSpan<SkBlock>(fTextStyles.data(), fTextStyles.size()));
  ShapeHandler handler(*this);
  SkShaper shaper(nullptr);
  shaper.shape(&handler,
               &font,
               fUtf8.begin(),
               fUtf8.size(),
               true,
               {0, 0},
               std::numeric_limits<SkScalar>::max());

  SkASSERT(fLines.empty());
  fMaxIntrinsicWidth = handler.advance().fX;
}

void SkParagraph::markClustersWithLineBreaks() {

  class SkTextBreaker {

   public:
    SkTextBreaker() : fPos(-1) {
    }

    bool initialize(SkSpan<const char> text, UBreakIteratorType type) {
      UErrorCode status = U_ZERO_ERROR;

      UText utf8UText = UTEXT_INITIALIZER;
      utext_openUTF8(&utf8UText, text.begin(), text.size(), &status);
      fAutoClose =
          std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>>(&utf8UText);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create utf8UText: %s", u_errorName(status));
        return false;
      }
      fIterator = ubrk_open(type, "th", nullptr, 0, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not create line break iterator: %s",
                 u_errorName(status));
        SK_ABORT("");
      }

      ubrk_setUText(fIterator, &utf8UText, &status);
      if (U_FAILURE(status)) {
        SkDebugf("Could not setText on break iterator: %s",
                 u_errorName(status));
        return false;
      }
      return true;
    }

    size_t next(size_t pos) {
      fPos = ubrk_following(fIterator, SkToS32(pos));
      return fPos;
    }

    int32_t status() { return ubrk_getRuleStatus(fIterator); }

    bool eof() { return fPos == icu::BreakIterator::DONE; }

    ~SkTextBreaker() = default;

   private:
    std::unique_ptr<UText, SkFunctionWrapper<UText*, UText, utext_close>> fAutoClose;
    UBreakIterator* fIterator;
    int32_t fPos;
  };

  // Find all clusters with line breaks
  SkTextBreaker breaker;
  if (!breaker.initialize(fUtf8, UBRK_LINE)) {
    return;
  }
  SkCluster* previous = nullptr;
  size_t currentPos = 0;
  iterateThroughClusters(
     [&breaker, this, &currentPos, &previous](SkCluster& cluster, bool last) {
        if (cluster.fText.end() < fUtf8.begin() + currentPos) {
          // Skip it until we get closer
          SkDebugf("  %d-%d: %f '%s'\n",
              cluster.fStart, cluster.fEnd, cluster.fWidth,  toString(cluster.fText).c_str());
          return;
        } else  if (cluster.fText.end() > fUtf8.begin() + currentPos) {
          currentPos = breaker.next(currentPos);
        }
        if (cluster.fText.end() == fUtf8.begin() + currentPos || last) {
         // Make sure every line break is also a cluster break
          cluster.fBreakType =
               breaker.status() == UBRK_LINE_HARD || breaker.eof()
               ? SkCluster::BreakType::HardLineBreak
               : SkCluster::BreakType::SoftLineBreak;
         cluster.setIsWhiteSpaces();
        }
       previous = &cluster;
       SkDebugf("%s%s%d-%d: %f '%s'\n",
           (cluster.canBreakLineAfter() ? "#" : " "),
           (cluster.isWhitespaces() ? "@" : " "),
           cluster.fStart, cluster.fEnd, cluster.fWidth,  toString(cluster.fText).c_str());
     });
}

void SkParagraph::breakShapedTextIntoLinesByClusters(SkScalar maxWidth, size_t maxLines) {

  // Add ICU breaker information to the clusters
  SkVector lineAdvance = SkVector::Make(0, 0);
  SkVector tailAdvance = SkVector::Make(0, 0);
  SkVector lineOffset = SkVector::Make(0, 0);
  SkCluster* bestLineBreak = nullptr;
  const char* lineStart = fUtf8.begin();
  bool hardLineBreak = false;

  iterateThroughClusters(
      [this, maxWidth, &lineOffset, &lineAdvance, &tailAdvance, &bestLineBreak, &lineStart, &hardLineBreak]
      (SkCluster& cluster, bool last) {

      auto addLine =
          [this, &lineOffset, &lineAdvance, &lineStart, &bestLineBreak, &hardLineBreak]
          (SkCluster* cluster, bool toTheEnd) {

        hardLineBreak = false;
        auto lineEnd = toTheEnd ? cluster->fText.end() : cluster->fText.begin();
        // Trim the trailing spaces
        if (toTheEnd && cluster->isWhitespaces()) {
          if (lineAdvance.fX == cluster->fWidth && cluster->fBreakType != SkCluster::BreakType::HardLineBreak) {
            // Do not draw spaces
            lineAdvance = SkVector::Make(0, 0);
            bestLineBreak = nullptr;
            return;
          }
          lineAdvance.fX -= cluster->fWidth;
          lineEnd = cluster->fText.begin();
        }
        fLines.emplace_back(lineOffset, lineAdvance, SkSpan<const char>(lineStart, lineEnd - lineStart));
        lineStart = toTheEnd ? cluster->fText.end() : cluster->fText.begin();
        lineOffset.fY += lineAdvance.fY;
        // Shift the rest of the line horizontally to the left
        // to compensate for the run positions since we broke the line
        lineOffset.fX = - cluster->fRun->position(toTheEnd ? cluster->fEnd : cluster->fStart).fX;
        lineAdvance = SkVector::Make(0, 0);
        bestLineBreak = nullptr;
      };

      auto clusterTrimmedWidth = cluster.isWhitespaces() ? 0 : cluster.fWidth;

      if (lineAdvance.fX + tailAdvance.fX + clusterTrimmedWidth > maxWidth) {
        // The current cluster does not fit the line (break it if there was anything at all)
        if (bestLineBreak != nullptr) {
          addLine(bestLineBreak, true);
        }
      }

      if (tailAdvance.fX + clusterTrimmedWidth > maxWidth) {
        // We cannot break this text properly by soft line breaks
        SkASSERT(lineAdvance.fX == 0);
        lineAdvance = tailAdvance;
        addLine(&cluster, false);
        tailAdvance = SkVector::Make(0, 0);
      }

      if (clusterTrimmedWidth > maxWidth) {
        // We cannot break this text even by clusters; clipping for now?...
        SkASSERT(lineAdvance.fX == 0 && tailAdvance.fX == 0);
        lineAdvance = SkVector::Make(maxWidth, cluster.fHeight);
        addLine(&cluster, true);
        return;
      }

      // The cluster fits
      tailAdvance.fX += cluster.fWidth;
      tailAdvance.fY = SkTMax(lineAdvance.fY, cluster.fHeight);

      if (cluster.canBreakLineAfter() || last) {
        bestLineBreak = &cluster;
        lineAdvance.fX += tailAdvance.fX;
        lineAdvance.fY = SkTMax(lineAdvance.fY, tailAdvance.fY);
        tailAdvance = SkVector::Make(0, 0);
      }

      // Hard line break or the last line
      if (last || cluster.fBreakType == SkCluster::BreakType::HardLineBreak) {
        addLine(bestLineBreak, true);
        hardLineBreak = true;
      }
  });
}

void SkParagraph::shapeIntoLines(SkScalar maxWidth, size_t maxLines) {

  resetContext();

  shapeTextIntoEndlessLine();

  buildClusterTable();

  markClustersWithLineBreaks();

  breakShapedTextIntoLinesByClusters(maxWidth, maxLines);
}

void SkParagraph::formatLinesByWords(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  for (auto& line : fLines) {

    if (effectiveAlign == SkTextAlign::justify && &line == &fLines.back()) {
      effectiveAlign = SkTextAlign::left;
    }
    line.formatByWords(effectiveAlign, maxWidth);
    fWidth = SkMaxScalar(fWidth, line.advance().fX);
    fHeight += line.advance().fY;
  }
}

// TODO: implement
std::vector<SkTextBox> SkParagraph::getRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rectHeightStyle,
    RectWidthStyle rectWidthStyle) {

  std::vector<SkTextBox> result;
  /*
  for (auto words = fUnbreakableWords.begin(); words != fUnbreakableWords.end(); ++words) {
    if (words->full().end() <= fUtf8.begin() + start || words->full().begin() >= fUtf8.begin() + end) {
      continue;
    }
    words->getRectsForRange(fParagraphStyle.getTextDirection(), fUtf8.begin() + start, fUtf8.begin() + end, result);
  }
   */
  return result;
}

// TODO: Optimize the search
size_t SkParagraph::findCluster(const char* ch) const {
  for (size_t i = 0; i < fClusters.size(); ++i) {
    auto& cluster = fClusters[i];
    if (cluster.fText.end() > ch) {
      SkASSERT(cluster.fText.begin() <= ch);
      return i;
    }
  }
  return fClusters.size();
}

SkScalar SkParagraph::findOffset(const char* ch) const {

  size_t index = findCluster(ch);
  SkASSERT(index < fClusters.size());

  auto cluster = fClusters[index];
  SkASSERT(cluster.fText.begin() == ch);

  return cluster.fRun->position(cluster.fStart).fX;
}

SkVector SkParagraph::measureText(SkSpan<const char> text) const {

  SkVector size = SkVector::Make(0, 0);
  if (text.empty()) {
    return size;
  }

  auto start = findCluster(text.begin());
  auto end = findCluster(text.end() - 1);
  for (auto cl = start; cl <= end; ++cl) {

    SkASSERT(cl < fClusters.size());
    auto& cluster = fClusters[cl];

    if (cl == start) {
      size.fX -= cluster.sizeToChar(text.begin());
    }
    if (cl == end) {
      size.fX += cluster.sizeFromChar(text.end() - 1);
    } else {
      size.fX += cluster.fWidth;
    }
    size.fY = SkTMax(size.fY, cluster.fHeight);
  }

  return size;
}

void SkParagraph::measureWords(SkWords& words) const {

  auto full = measureText(words.full());
  auto trimmed = measureText(words.trimmed());

  words.setSizes(full, trimmed.fX);
}

void SkParagraph::iterateThroughStyles(
    const SkSpan<const char> text,
    SkStyleType styleType,
    std::function<void(SkSpan<const char> text, SkTextStyle style)> apply) const {

  const char* start = nullptr;
  size_t size = 0;
  SkTextStyle prevStyle;
  for (auto& textStyle : fTextStyles) {

    if (!(textStyle.text() && text)) {
      continue;
    }
    auto style = textStyle.style();
    auto begin = SkTMax(textStyle.text().begin(), text.begin());
    auto end = SkTMin(textStyle.text().end(), text.end());
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
    apply(SkSpan<const char>(start, size), prevStyle);
    // Start all over again
    prevStyle = style;
    start = intersect.begin();
    size = intersect.size();
  }

  apply(SkSpan<const char>(start, size), prevStyle);
}

// TODO: Optimize the search
void SkParagraph::iterateThroughRuns(
    SkSpan<const char> text,
    std::function<void(const SkRun* run, size_t pos, size_t size, SkRect clip)> apply) const {

  auto start = findCluster(text.begin());
  auto end = findCluster(text.end() - 1);

  SkRect clip = SkRect::MakeEmpty();
  size_t size = 0;
  size_t pos = 0;

  const SkRun* run = nullptr;
  for (auto cl = start; cl <= end; ++cl) {
    auto& cluster = fClusters[cl];

    if (run != cluster.fRun) {
      if (run != nullptr) {
        apply(run, pos, size, clip);
      }
      run = cluster.fRun;
      clip = SkRect::MakeXYWH(run->offset().fX, run->offset().fY, 0, 0);
      size = 0;
      pos = cluster.fStart;
    }

    size += (cluster.fEnd - cluster.fStart);
    if (cl == start) {
      clip.fLeft = cluster.fRun->position(cluster.fStart).fX;
      clip.fRight = clip.fLeft;
      clip.fLeft += cluster.sizeToChar(text.begin());
    }
    if (cl == end) {
      clip.fRight += cluster.sizeFromChar(text.end() - 1);
    } else {
      clip.fRight += cluster.fWidth;
    }
    clip.fBottom = SkTMax(clip.fBottom, cluster.fHeight);
  }

  //clip.offset(run->offset());

  //SkDebugf("Clip: @%d,%d [%f:%f]\n", pos, size, clip.fLeft, clip.fRight);
  apply(run, pos, size, clip);
}

void SkParagraph::iterateThroughClusters(
    std::function<void(SkCluster& cluster, bool last)> apply) {

  size_t index = 0;
  while (index < fClusters.size() && fClusters[index].fText.begin() > fUtf8.begin()) ++index;

  while (index < fClusters.size()) {
    auto& cluster = fClusters[index];
    if (cluster.fText.begin() > fUtf8.end()) {
      break;
    }
    apply(cluster, index == fClusters.size() - 1);
    ++index;
  }
}

SkPositionWithAffinity SkParagraph::getGlyphPositionAtCoordinate(double dx, double dy) const {
  // TODO: implement
  return {0, Affinity::UPSTREAM};
}

SkRange<size_t> SkParagraph::getWordBoundary(unsigned offset) {
  // TODO: implement
  SkRange<size_t> result;
  return result;
}