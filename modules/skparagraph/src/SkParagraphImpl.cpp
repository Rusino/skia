/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <unicode/brkiter.h>
#include <SkBlurTypes.h>
#include "SkSpan.h"
#include "SkParagraphImpl.h"
#include "SkPictureRecorder.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "SkCanvas.h"
#include "SkMaskFilter.h"

namespace {
  std::string toString(SkSpan<const char> text) {
    icu::UnicodeString
        utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
    std::string str;
    utf16.toUTF8String(str);
    return str;
  }
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
  fLines.reset();
  fRuns.reset();
  fClusters.reset();
  fIndexes.reset();
}

bool SkParagraphImpl::layout(double doubleWidth) {
  
  auto width = SkDoubleToScalar(doubleWidth);

  this->resetContext();

  this->shapeTextIntoEndlessLine(fUtf8, SkSpan<SkBlock>(fTextStyles.begin(), fTextStyles.size()));

  this->buildClusterTable();

  this->markClustersWithLineBreaks();

  this->breakShapedTextIntoLines(width);

  this->formatLinesByText(width);

  return true;
}

void SkParagraphImpl::paint(SkCanvas* canvas, double x, double y) {

  if (nullptr == fPicture) {
    // Build the picture lazily not until we actually have to paint (or never)
    this->formatLinesByWords(fWidth);
    SkPictureRecorder recorder;
    SkCanvas* textCanvas = recorder.beginRecording(fWidth, fHeight, nullptr, 0);
    for (auto& line : fLines) {

      if (line.empty()) continue;

      auto lineOffset = line.offset();
      textCanvas->save();
      textCanvas->translate(lineOffset.fX, lineOffset.fY);
      this->iterateThroughStyles(line, SkStyleType::Background,
       [this, textCanvas](SkSpan<const char> text, SkTextStyle style, SkRun* ellipsis) {
         this->paintBackground(textCanvas, text, style, ellipsis);
         return true;
      });

      this->iterateThroughStyles(line, SkStyleType::Shadow,
       [this, textCanvas](SkSpan<const char> text, SkTextStyle style, SkRun* ellipsis) {
         this->paintShadow(textCanvas, text, style, ellipsis);
         return true;
      });

      this->iterateThroughStyles(line, SkStyleType::Foreground,
       [this, textCanvas](SkSpan<const char> text, SkTextStyle style, SkRun* ellipsis) {
         this->paintText(textCanvas, text, style, ellipsis);
         return true;
      });

      this->iterateThroughStyles(line, SkStyleType::Decorations,
       [this, textCanvas](SkSpan<const char> text, SkTextStyle style, SkRun* ellipsis) {
         this->paintDecorations(textCanvas, text, style, ellipsis);
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
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkRun* ellipsis) const {

  SkPaint paint;
  if (style.hasForeground()) {
    paint = style.getForeground();
  } else {
    paint.reset();
    paint.setColor(style.getColor());
  }
  paint.setAntiAlias(true);

  // Build the blob from all the runs
  this->iterateThroughRuns(text, ellipsis,
   [paint, canvas](const SkRun* run, int32_t pos, size_t size, SkRect rect, SkScalar shift) {
     SkTextBlobBuilder builder;
     run->copyTo(builder, pos, size);
     canvas->save();
     canvas->clipRect(rect);
     canvas->translate(shift, 0);
     canvas->drawTextBlob(builder.make(), 0, 0, paint);
     canvas->restore();
     return true;
   });
}

void SkParagraphImpl::paintBackground(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkRun* ellipsis) const {

  if (!style.hasBackground()) return;
  this->iterateThroughRuns(
      text, ellipsis,
      [canvas, style](const SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift) {
        canvas->drawRect(clip, style.getBackground());
        return true;
      });
}

void SkParagraphImpl::paintShadow(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkRun* ellipsis) const {

  if (style.getShadowNumber() == 0) return;

  for (SkTextShadow shadow : style.getShadows()) {

    if (!shadow.hasShadow()) continue;

    SkPaint paint;
    paint.setColor(shadow.fColor);
    if (shadow.fBlurRadius != 0.0) {
      auto filter = SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, shadow.fBlurRadius, false);
      paint.setMaskFilter(filter);
    }

    this->iterateThroughRuns(text, ellipsis,
     [canvas, shadow, paint](const SkRun* run, size_t pos, size_t size, SkRect rect, SkScalar shift) {
       SkTextBlobBuilder builder;
       run->copyTo(builder, pos, size);
       canvas->save();
       canvas->clipRect(rect.makeOffset(shadow.fOffset.x(), shadow.fOffset.y()));
       canvas->translate(shift, 0);
       canvas->drawTextBlob(builder.make(), shadow.fOffset.x(), shadow.fOffset.y(), paint);
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
  paint.setAntiAlias(true);

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

// TODO: Make the thickness reasonable
void SkParagraphImpl::paintDecorations(
    SkCanvas* canvas,
    SkSpan<const char> text,
    const SkTextStyle& style,
    SkRun* ellipsis) const {

  if (style.getDecoration() == SkTextDecoration::kNone) return;

  iterateThroughRuns(text,
     ellipsis,
     [this, canvas, style](const SkRun* run, int32_t pos, size_t size, SkRect clip, SkScalar shift) {

       SkScalar thickness = style.getDecorationThicknessMultiplier();
       SkScalar position;
       switch (style.getDecoration()) {
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
           canvas->drawLine(x,
                            y,
                            x + width,
                            y,
                            paint);
           break;
         default:
           break;
       }
       return true;
     });
}

void SkParagraphImpl::buildClusterTable() {

  for (auto& run : fRuns) {
    size_t cluster = 0;
    SkScalar width = 0;
    size_t start = 0;
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
      fIndexes.set(run.text().begin() + cluster, fClusters.size() - 1);

      cluster = next;
      start = pos;
      width = 0;
    }
  }
}

void SkParagraphImpl::shapeTextIntoEndlessLine(SkSpan<const char> text, SkSpan<SkBlock> styles) {

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
    explicit ShapeHandler(SkParagraphImpl& paragraph)
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
      auto& run = fParagraph->fRuns.emplace_back(font, info, glyphCount, utf8);
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

    SkParagraphImpl* fParagraph;
    SkVector fAdvance;
  };

  MultipleFontRunIterator font(text, styles);
  ShapeHandler handler(*this);
  SkShaper shaper(nullptr);
  shaper.shape(&handler,
               &font,
               text.begin(),
               text.size(),
               true,
               {0, 0},
               std::numeric_limits<SkScalar>::max());

  fMaxIntrinsicWidth = handler.advance().fX;
}

SkRun* SkParagraphImpl::shapeEllipsis(SkRun* run) {

  class ShapeHandler final : public SkShaper::RunHandler {

   public:
    ShapeHandler(SkParagraphImpl* master) : fMaster(master), fRun(nullptr) { }
    SkRun* run() { return fRun; }

   private:
    // SkShaper::RunHandler interface
    SkShaper::RunHandler::Buffer newRunBuffer(
        const RunInfo& info,
        const SkFont& font,
        int glyphCount,
        SkSpan<const char> utf8) override {
      // Runs always go to the end of the list even if we insert words in the middle
      fRun = fMaster->fEllipsis.set(font, SkRun(font, info, glyphCount, utf8));
      return fRun->newRunBuffer();
    }

    void commitRun() override { }
    void commitLine() override { }

    SkParagraphImpl* fMaster;
    SkRun* fRun;
  };

  auto ellipsis = fParagraphStyle.getEllipsis();
  ShapeHandler handler(this);
  SkShaper shaper(nullptr);
  shaper.shape(&handler,
               run->font(),
               ellipsis.data(),
               ellipsis.size(),
               true,
               {0, 0},
               std::numeric_limits<SkScalar>::max());

  return handler.run();
}

void SkParagraphImpl::markClustersWithLineBreaks() {

  // Find all clusters with line breaks
  SkTextBreaker breaker;
  if (!breaker.initialize(fUtf8, UBRK_LINE)) {
    return;
  }

  size_t currentPos = 0;
  for (auto& cluster : fClusters) {
    auto last = &cluster == &fClusters.back();
    if (cluster.fText.end() < fUtf8.begin() + currentPos) {
      // Skip it until we get closer
      SkDebugf("  %d-%d: %f '%s'\n",
               cluster.fStart,
               cluster.fEnd,
               cluster.fWidth,
               toString(cluster.fText).c_str());
      continue;
    } else if (cluster.fText.end() > fUtf8.begin() + currentPos) {
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
    SkDebugf("%s%s%d-%d: %f '%s'\n",
             (cluster.canBreakLineAfter() ? "#" : " "),
             (cluster.isWhitespaces() ? "@" : " "),
             cluster.fStart,
             cluster.fEnd,
             cluster.fWidth,
             toString(cluster.fText).c_str());
  }
}

void SkParagraphImpl::breakShapedTextIntoLines(SkScalar maxWidth) {

  class TextWrapper {
   public:
    class Position {
     public:
      Position(const SkCluster* start) {
        clean(start);
      }
      inline SkScalar width() const { return fWidth + fWhitespaces; }
      inline SkScalar trimmedWidth() const { return fWidth; }
      inline SkScalar height() const { return fHeight; }
      inline const SkCluster* trimmed() { return fTrimmedEnd; }
      inline const SkCluster* end() { return fEnd; }
      inline SkVector trimmedAdvance() { return SkVector::Make(fWidth, fHeight); }
      void clean(const SkCluster* start) {
        fEnd = start;
        fTrimmedEnd = start;
        fWidth = 0;
        fHeight = 0;
        fWhitespaces = 0;
      }
      void add(Position& other) {
        this->fWidth += this->fWhitespaces + other.fWidth;
        this->fHeight = SkTMax(this->fHeight, other.fHeight);
        this->fTrimmedEnd = other.fTrimmedEnd;
        this->fEnd = other.fEnd;
        this->fWhitespaces = other.fWhitespaces;
        other.clean(other.fEnd);
      }
      void add(const SkCluster& cluster) {
        if (cluster.isWhitespaces()) {
          fWhitespaces += cluster.fWidth;
        } else {
          fTrimmedEnd = &cluster;
          fWidth += cluster.fWidth + fWhitespaces;
          fWhitespaces = 0;
        }
        fEnd = &cluster;
        fHeight = SkTMax(fHeight, cluster.fHeight);
      }
      void extend(SkScalar w) { fWidth += w; }
      SkSpan<const char> trimmedText(const SkCluster* start) {
        return SkSpan<const char>(start->fText.begin(), fTrimmedEnd->fText.end() - start->fText.begin());
      }
     private:
      SkScalar fWidth;
      SkScalar fHeight;
      SkScalar fWhitespaces;
      const SkCluster* fEnd;
      const SkCluster* fTrimmedEnd;
    };
    TextWrapper(SkParagraphImpl* master, SkScalar maxWidth)
      : fMaster(master)
      , fMaxWidth(maxWidth)
      , fLineStart(master->fClusters.begin())
      , fClosestBreak(master->fClusters.begin())
      , fAfterBreak(master->fClusters.begin())
      , fOffset(SkVector::Make(0, 0))
      , fAdvance(SkVector::Make(0, 0)) { }

    SkRun* createEllipsis(Position& pos) {
      if (!fMaster->reachedLinesLimit(-1) || pos.end() == &fMaster->fClusters.back()) {
        // We must be on the last line and not at the end of the text
        return nullptr;
      }
      // Replace some clusters with the ellipsis
      auto lineEnd = pos.trimmed();
      while (lineEnd >= fLineStart) {
        // Calculate the ellipsis sizes for a given font
        SkRun* ellipsis = fMaster->getEllipsis(lineEnd->fRun);
        if (pos.trimmedWidth() + ellipsis->advance().fX <= fMaxWidth) {
          // Ellipsis fit; place and size it correctly
          ellipsis->shift(-fOffset.fX + pos.trimmedWidth());
          ellipsis->setHeight(pos.height());
          pos.extend(ellipsis->advance().fX);
          return ellipsis;
        }
        // It is possible that the ellipsis is wider than the line itself for a given font;
        // we still need to continue because we can find a smaller font and it will fit
        pos.extend(-lineEnd->fWidth);
        --lineEnd;
      }
      return nullptr;
    }
    bool endOfText() const { return fLineStart == fMaster->fClusters.end(); }
    bool addLine(Position& pos) {
      if (pos.trimmedWidth() == 0 && !pos.end()->isHardBreak()) {
        // Ignore an empty line if it's not generated by hard line break
        pos.clean(pos.end());
        return true;
      }
      auto ellipsis = createEllipsis(pos);
      fMaster->addLine(fOffset, pos.trimmedAdvance(), pos.trimmedText(fLineStart), ellipsis);
      fLineStart = pos.end() + 1;
      if (!pos.end()->isHardBreak()) {
        while (fLineStart < fMaster->fClusters.end() &&
            fLineStart->isWhitespaces()) { ++fLineStart; }
      }
      fOffset.fY += pos.height();
      if (fLineStart < fMaster->fClusters.end()) {
        // Shift the rest of the line horizontally to the left
        // to compensate for the run positions since we broke the line
        fOffset.fX = - fLineStart->fRun->position(fLineStart->fStart).fX;
      }
      pos.clean(fLineStart);
      return !fMaster->reachedLinesLimit();
    }
    void formatText() {
      // Iterate through all the clusters in the text
      for (auto& cluster : fMaster->fClusters) {

        if (!cluster.isWhitespaces()) {
          if (fClosestBreak.width() + fAfterBreak.width() + cluster.fWidth > fMaxWidth) {
            // Cluster does not fit: add the line until the closest break
            if (!addLine(fClosestBreak))  break;
          }
          if (fAfterBreak.width() + cluster.fWidth > fMaxWidth) {
            // Cluster still does not yet: add the line with the rest of clusters
            SkASSERT(fClosestBreak.width() == 0);
            fClosestBreak.add(fAfterBreak);
            if (!addLine(fClosestBreak))  break;
          }
          if (cluster.fWidth > fMaxWidth) {
            //  Cluster still does not fit: it's too long; let's clip it
            fClosestBreak.add(cluster);
            if (!addLine(fClosestBreak))  break;
            continue;
          }
        }
        // The cluster fits the line
        fAfterBreak.add(cluster);
        if (cluster.canBreakLineAfter()) {
          fClosestBreak.add(fAfterBreak);
        }
        if (cluster.isHardBreak()) {
          // Hard line break
          if (!addLine(fClosestBreak))  break;
        }
      }
      // Make sure nothing left
      if (!endOfText()) {
        fClosestBreak.add(fAfterBreak);
        addLine(fClosestBreak);
      }
    }

   private:
    SkParagraphImpl* fMaster;
    SkScalar fMaxWidth;
    const SkCluster* fLineStart;
    Position fClosestBreak;
    Position fAfterBreak;
    SkVector fOffset;
    SkVector fAdvance;
  };

  TextWrapper(this, maxWidth).formatText();
}

void SkParagraphImpl::formatLinesByText(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  if (effectiveAlign == SkTextAlign::justify) {
    fWidth = maxWidth;
  }
}

void SkParagraphImpl::formatLinesByWords(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  for (auto& line : fLines) {

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

        if (&line != &fLines.back()) {
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

void SkParagraphImpl::justifyLine(SkLine& line, SkScalar maxWidth) {

  SkScalar len = 0;
  line.breakLineByWords(UBRK_LINE, [this, &len](SkWord& word) {
    word.fAdvance = this->measureText(word.text());
    word.fShift = len;
    len += word.fAdvance.fX;
  });

  // TODO: find all whitespace clusters in the line and use them instead of words
  // TODO: increase each whitespace cluster size to spread the words across the line
  // TODO: Add position info to the cluster and update it instead of the runs
  // TODO: Take position info from the clusters instead of the runs when calculating clipping regions

  auto delta = maxWidth - len;
  auto softLineBreaks = line.fWords.size() - 1;
  if (softLineBreaks == 0) {
    auto word = line.fWords.begin();
    word->expand(delta);
    // TODO: for one word justification cannot calculate the size of the line correctly
    // TODO: (since we do not correct positions)
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
    this->iterateThroughRuns(word.text(), nullptr,
       [shift](SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar) {
      for (auto i = pos; i < pos + size; ++i) {
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

SkRun* SkParagraphImpl::getEllipsis(SkRun* run) {

  SkRun* found = fEllipsis.find(run->font());
  if (found != nullptr) {
    return found;
  }

  found = shapeEllipsis(run);
  return found;
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
    std::function<bool(SkSpan<const char> text, const SkTextStyle& style, SkRun* ellipsis)> apply) const {

  const char* start = nullptr;
  size_t size = 0;
  SkTextStyle prevStyle;
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
    if (!apply(SkSpan<const char>(start, size), prevStyle, nullptr)) {
      return;
    }
    // Start all over again
    prevStyle = style;
    start = intersect.begin();
    size = intersect.size();
  }

  // The very last style
  if (size > 0) {
    apply(SkSpan<const char>(start, size), prevStyle, line.fEllipsis);
  }
}

void SkParagraphImpl::iterateThroughRuns(
    SkSpan<const char> text,
    SkRun* ellipsis,
    std::function<bool(SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift)> apply) const {

  auto start = findCluster(text.begin());
  auto end = findCluster(text.end() - 1);

  SkRect clip = SkRect::MakeEmpty();
  size_t size = 0;
  size_t pos = 0;
  SkRun* run = nullptr;
  for (auto cluster = start; cluster <= end; ++cluster) {

    if (run != cluster->fRun) {
      if (run != nullptr) {
        if (!apply(run, pos, size, clip, 0)) {
          return;
        }
      }
      run = cluster->fRun;
      clip = SkRect::MakeXYWH(run->offset().fX, run->offset().fY, 0, 0);
      size = 0;
      pos = cluster->fStart;
    }

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
    clip.fBottom = SkTMax(clip.fBottom, cluster->fHeight);
  }

  // The very last call
  apply(run, pos, size, clip, 0);
  if (ellipsis != nullptr) {
    apply(ellipsis, 0, ellipsis->size(), ellipsis->clip(), ellipsis->offset().fX);
  }
}


// TODO: Height & Width styles
std::vector<SkTextBox> SkParagraphImpl::getRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rectHeightStyle,
    RectWidthStyle rectWidthStyle) {

  SkSpan<const char> text(fUtf8.begin() + start, end - start);
  std::vector<SkTextBox> result;
  for (auto& line : fLines) {
    if (line.fText && text) {
      auto begin = SkTMax(line.fText.begin(), text.begin());
      auto end = SkTMin(line.fText.end(), text.end());
      auto intersect = SkSpan<const char>(begin, end - begin);

      auto size = measureText(intersect);
      SkRect rect = SkRect::MakeXYWH(0, 0, size.fX, size.fY);
      rect.offset(line.fShift, 0);
      rect.offset(line.fOffset);
      result.emplace_back(rect, fParagraphStyle.getTextDirection());
    }
  }
  return result;
}

// TODO: Text direction
SkPositionWithAffinity SkParagraphImpl::getGlyphPositionAtCoordinate(double dx, double dy) const {

  SkPositionWithAffinity result(-1, Affinity::UPSTREAM);
  for (auto& line : fLines) {
    if (line.fOffset.fY <= dy && dy < line.fOffset.fY + line.fAdvance.fY) {
      this->iterateThroughRuns(
          line.fText,
          nullptr,
          [dx, &result](const SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift) {
            auto offset = run->offset();
            auto advance = run->advance();
            if (offset.fX <= dx && dx < offset.fX + advance.fX) {
              for (size_t i = 0; i <= run->size(); ++i) {
                if (run->position(i).fX < dx) {
                  result.position = i;
                  return false;
                }
              }
            }
            return true;
      });
    }
  }
  return result;
}

SkRange<size_t> SkParagraphImpl::getWordBoundary(unsigned offset) {

  SkSpan<const char> text(fUtf8.begin() + offset, 1);
  for (auto& line : fLines) {
    if (line.fText && text) {
      for (auto& word : line.fWords) {
        if (word.fText && text) {
          return SkRange<size_t>(word.fText.begin() - fUtf8.begin(), word.fText.end() - fUtf8.begin());
        }
      }
    }
  }
  return SkRange<size_t>();
}