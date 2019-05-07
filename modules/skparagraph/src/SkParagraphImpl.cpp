/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <unicode/brkiter.h>
#include <SkBlurTypes.h>
#include <SkFontMgr.h>
#include <unicode/ubidi.h>
#include "SkSpan.h"
#include "SkParagraphImpl.h"
#include "SkPictureRecorder.h"
#include "SkCanvas.h"
#include "SkUTF.h"

namespace {
/*
  std::string toString(SkSpan<const char> text) {
    icu::UnicodeString
        utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
    std::string str;
    utf16.toUTF8String(str);
    return str;
  }
  */
  void print(const SkCluster& cluster) {
/*
    auto type = cluster.breakType() == SkCluster::BreakType::HardLineBreak
                ? "!"
                : (cluster.isSoftBreak() ? "?" : " ");
    SkDebugf("Cluster %s%s", type, cluster.isWhitespaces() ? "*" : " ");
    SkDebugf("[%d:%d) ", cluster.startPos(), cluster.endPos());
    SkDebugf(" %f ", cluster.width());

    SkDebugf("'");
    for (auto ch = cluster.text().begin(); ch != cluster.text().end(); ++ch) {
      SkDebugf("%c", *ch);
    }
    SkDebugf("'");

    if (cluster.text().size() != 1) {
      SkDebugf("(%d)\n", cluster.text().size());
    } else {
      SkDebugf("\n");
    }
    */
  }
  /*
  SkSpan<const char> operator*(const SkSpan<const char>& a, const SkSpan<const char>& b) {
    auto begin = SkTMax(a.begin(), b.begin());
    auto end = SkTMin(a.end(), b.end());
    return SkSpan<const char>(begin, end > begin ? end - begin : 0);
  }
*/
  static inline SkUnichar utf8_next(const char** ptr, const char* end) {
    SkUnichar val = SkUTF::NextUTF8(ptr, end);
    return val < 0 ? 0xFFFD : val;
  }
}

SkParagraphImpl::~SkParagraphImpl() = default;

void SkParagraphImpl::layout(SkScalar width) {

  this->resetContext();

  this->shapeTextIntoEndlessLine();

  this->buildClusterTable();

  this->breakShapedTextIntoLines(width);
}

void SkParagraphImpl::paint(SkCanvas* canvas, SkScalar x, SkScalar y) {

  if (nullptr == fPicture) {
    // Build the picture lazily not until we actually have to paint (or never)
    this->formatLines(fWidth);
    this->paintLinesIntoPicture();
  }

  SkMatrix matrix = SkMatrix::MakeTrans(x, y);
  canvas->drawPicture(fPicture, &matrix, nullptr);
}

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
  fLines.reset();
  fTextWrapper.reset();
}

// Clusters in the order of the input text
void SkParagraphImpl::buildClusterTable() {

  // Find all possible (soft) line breaks
  SkTextBreaker breaker;
  if (!breaker.initialize(fUtf8, UBRK_LINE)) {
    return;
  }
  size_t currentPos = breaker.first();
  SkTHashMap<const char*, bool> map;
  while (!breaker.eof()) {
    currentPos = breaker.next();
    const char* ch = currentPos + fUtf8.begin();
    map.set(ch, breaker.status() == UBRK_LINE_HARD);
  }

  SkBlock* currentStyle = this->fTextStyles.begin();
  SkScalar spacing = 0;
  // Walk through all the run in the direction of input text
  std::vector<std::tuple<SkRun*, size_t, size_t>> toUpdate;
  for (auto& run : fRuns) {

    auto runStart = fClusters.size();
    // Walk through the glyph in the direction of input text
    run.iterateThroughClustersInTextOrder(
      [&run, this, &map, &currentStyle, &spacing]
      (size_t glyphStart,
       size_t glyphEnd,
       size_t charStart,
       size_t charEnd,
       SkScalar width,
       SkScalar height) {

        SkASSERT(charEnd >= charStart);
        SkSpan<const char> text(fUtf8.begin() + charStart, charEnd - charStart);

        auto& cluster = fClusters.emplace_back(&run, glyphStart, glyphEnd, text, width, height);
        // Mark line breaks
        auto found = map.find(cluster.text().end());
        if (found) {
          cluster.setBreakType(*found
                               ? SkCluster::BreakType::HardLineBreak
                               : SkCluster::BreakType::SoftLineBreak);
        }
        cluster.setIsWhiteSpaces();

        // Synchronize styles (one cluster can be covered by few styles)
        while (!cluster.startsIn(currentStyle->text())) {
          currentStyle++;
          SkASSERT(currentStyle != this->fTextStyles.end());
        }

        if (currentStyle->style().getLetterSpacing() != 0) {
          run.space(spacing, currentStyle->style().getLetterSpacing(), &cluster);
        }

        print(cluster);
      });
    toUpdate.emplace_back(&run, runStart, fClusters.size() - runStart);
  }
  for (auto update : toUpdate) {
    auto run = std::get<0>(update);
    auto start = std::get<1>(update);
    auto size = std::get<2>(update);
    run->setClusters(SkSpan<SkCluster>(&fClusters[start], size));
  }

  //fClusters.back().setBreakType(SkCluster::BreakType::HardLineBreak);
}

void SkParagraphImpl::shapeTextIntoEndlessLine() {

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
        , fStyles(styles)
        , fFontCollection(fonts)
        , fHintingOn(hintingOn) {

      resolveFonts();
    }

    bool existsInFont(sk_sp<SkTypeface> typeface, SkUnichar u) {
      return typeface->unicharToGlyph(u) != 0;
    }

    // TODO: Resolve fonts via harfbuzz
    void resolveFonts() {
      fResolvedFont.reset();

      sk_sp<SkTypeface> prevTypeface;
      SkScalar prevFontSize = 0;
      SkScalar prevLineHeight = 0;
      while (fCurrentChar != fText.end()) {

        SkFontStyle currentFontStyle = fCurrentStyle->style().getFontStyle();
        SkScalar currentFontSize = fCurrentStyle->style().getFontSize();
        SkScalar currentLineHeight = fCurrentStyle->style().getHeight();

        const char* current = fCurrentChar;
        SkUnichar u = utf8_next(&fCurrentChar, fText.end());

        // Find the first typeface from the collection that exists in the current text style
        sk_sp<SkTypeface> typeface = nullptr;
        for (auto& fontFamily : fCurrentStyle->style().getFontFamilies()) {
          typeface = fFontCollection->matchTypeface(fontFamily, currentFontStyle);
          if (typeface.get() != nullptr && existsInFont(typeface, u)) {
            if (!SkTypeface::Equal(typeface.get(), prevTypeface.get()) ||
                prevFontSize != currentFontSize ||
                prevLineHeight != currentLineHeight) {
              fResolvedFont.set(current, makeFont(typeface, currentFontSize, currentLineHeight));
              prevTypeface = typeface;
              prevFontSize = currentFontSize;
              prevLineHeight = currentLineHeight;
            }
            break;
          }
        }
        if (prevTypeface.get() == nullptr) {
          // We didn't find anything
          SkTypeface* candidate = fFontCollection->geFallbackManager()->matchFamilyStyleCharacter(
                  nullptr, currentFontStyle, nullptr, 0, u);
          if (candidate) {
            // We found a font with this character
            prevTypeface.reset(candidate);
          } else if (typeface != nullptr && typeface.get() != nullptr) {
            // We cannot resolve the character we may as well continue with the current font
            prevTypeface = typeface;
          } else {
            // Anything...
            prevTypeface = SkTypeface::MakeDefault();
          }
          fResolvedFont.set(current, makeFont(prevTypeface, currentFontSize, currentLineHeight));
          prevFontSize = currentFontSize;
          prevLineHeight = currentLineHeight;
        }

        // Find the text style that contains the next character
        while (fCurrentStyle != fStyles.end() && fCurrentStyle->text().end() <= fCurrentChar) {
          ++fCurrentStyle;
        }
      }

      fCurrentChar = fText.begin();
      fCurrentStyle = fStyles.begin();
    }

    void consume() override {

      SkASSERT(fCurrentChar < fText.end());

      auto found = fResolvedFont.find(fCurrentChar);
      if (found == nullptr) {
        fFont = SkFont();
        fLineHeight = 1;
      } else {
        // Get the font
        fFont = found->first;
        fLineHeight = found->second;
      }

      // Find the first character (index) that cannot be resolved with the current font
      while (++fCurrentChar != fText.end()) {
        found = fResolvedFont.find(fCurrentChar);
        if (found != nullptr) {
          break;
        }
      }
    }

    std::pair<SkFont, SkScalar> makeFont(sk_sp<SkTypeface> typeface, SkScalar size, SkScalar height) {
      SkFont font(typeface, size);
      font.setEdging(SkFont::Edging::kAntiAlias);
      if (!fHintingOn) {
        font.setHinting(SkFontHinting::kSlight);
        font.setSubpixel(true);
      }
      return std::make_pair(font, height);
    }

    size_t endOfCurrentRun() const override { return fCurrentChar - fText.begin(); }
    bool atEnd() const override { return fCurrentChar == fText.end(); }
    const SkFont& currentFont() const override { return fFont; }
    SkScalar lineHeight() const { return fLineHeight; }

   private:
    SkSpan<const char> fText;
    const char* fCurrentChar;
    SkFont fFont;
    SkScalar fLineHeight;
    SkBlock* fCurrentStyle;
    SkSpan<SkBlock> fStyles;
    sk_sp<SkFontCollection> fFontCollection;
    SkTHashMap<const char*, std::pair<SkFont, SkScalar>> fResolvedFont;
    bool fHintingOn;
  };

 class ShapeHandler final : public SkShaper::RunHandler {

   public:
    explicit ShapeHandler(SkParagraphImpl& paragraph, MultipleFontRunIterator* fontIterator)
        : fParagraph(&paragraph)
        , fFontIterator(fontIterator)
        , fAdvance(SkVector::Make(0, 0)) {}

    inline SkVector advance() const { return fAdvance; }

   private:

    void beginLine() override { }

    void runInfo(const RunInfo&) override { }

    void commitRunInfo() override { }

    Buffer runBuffer(const RunInfo& info) override {

      auto& run = fParagraph->fRuns.emplace_back(
          fParagraph->text(),
          info,
          fFontIterator->lineHeight(),
          fParagraph->fRuns.count(),
          fAdvance.fX);
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
      fAdvance.fY = SkMaxScalar(fAdvance.fY, run.descent() - run.ascent());
    }

    void commitLine() override { }

    SkParagraphImpl* fParagraph;
    MultipleFontRunIterator* fFontIterator;
    SkVector fAdvance;
 };

  SkSpan<SkBlock> styles(fTextStyles.begin(), fTextStyles.size());
  MultipleFontRunIterator font(fUtf8, styles, fFontCollection, fParagraphStyle.hintingIsOn());
  ShapeHandler handler(*this, &font);
  std::unique_ptr<SkShaper> shaper = SkShaper::MakeShapeThenWrap();

  auto bidi = SkShaper::MakeIcuBiDiRunIterator(fUtf8.begin(), fUtf8.size(),
                          fParagraphStyle.getTextDirection() == SkTextDirection::ltr ? (uint8_t)2 : (uint8_t)1);
  auto script = SkShaper::MakeHbIcuScriptRunIterator(fUtf8.begin(), fUtf8.size());
  auto lang = SkShaper::MakeStdLanguageRunIterator(fUtf8.begin(), fUtf8.size());

  shaper->shape(fUtf8.begin(), fUtf8.size(),
               font,
               *bidi,
               *script,
               *lang,
               std::numeric_limits<SkScalar>::max(),
               &handler);

  fMaxIntrinsicWidth = handler.advance().fX;
}

void SkParagraphImpl::breakShapedTextIntoLines(SkScalar maxWidth) {

  fTextWrapper.formatText(
      SkSpan<SkCluster>(fClusters.begin(), fClusters.size()),
      maxWidth,
      fParagraphStyle.getMaxLines(),
      fParagraphStyle.getEllipsis());
  fHeight = fTextWrapper.height();
  fWidth = maxWidth; //fTextWrapper.width();
  fMinIntrinsicWidth = fTextWrapper.intrinsicWidth();
  fAlphabeticBaseline = fLines.empty() ? 0 : fLines.front().alphabeticBaseline();
  fIdeographicBaseline = fLines.empty() ? 0 : fLines.front().ideographicBaseline();
}

void SkParagraphImpl::formatLines(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  for (auto& line : fLines) {
    line.format(effectiveAlign, maxWidth, &line != &fLines.back());
  }
}

void SkParagraphImpl::paintLinesIntoPicture() {

  SkPictureRecorder recorder;
  SkCanvas* textCanvas = recorder.beginRecording(fWidth, fHeight, nullptr, 0);

  auto blocks = SkSpan<SkBlock>(fTextStyles.begin(), fTextStyles.size());
  for (auto& line : fLines) {
    line.paint(textCanvas, blocks);
  }

  fPicture = recorder.finishRecordingAsPicture();
}

SkLine& SkParagraphImpl::addLine (SkVector offset, SkVector advance, SkSpan<const char> text, SkLineMetrics sizes) {
  return fLines.emplace_back
      (offset,
       advance,
       SkSpan<SkCluster>(fClusters.begin(), fClusters.size()),
       text,
       sizes);
}

// Returns a vector of bounding boxes that enclose all text between
// start and end glyph indexes, including start and excluding end
std::vector<SkTextBox> SkParagraphImpl::getRectsForRange(
    unsigned start,
    unsigned end,
    RectHeightStyle rectHeightStyle,
    RectWidthStyle rectWidthStyle) {

  std::vector<SkTextBox> results;
  if (start >= end) {
    return results;
  }
  size_t index = 0;
  for (auto& line : fLines) {

    if (index >= end) {
      break;
    }

    auto firstBox = results.size();
    SkRect maxClip = SkRect::MakeLTRB(SK_ScalarMax, SK_ScalarMax, -SK_ScalarMax, -SK_ScalarMax);
    line.iterateThroughRuns(
      line.text(),
      false,
      [&results, &maxClip, &line, &index, start, end]
      (SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded) {
        if (size == 0 && pos == start) {
          // Special case: empty line
        } else if (pos + size <= start) {
          // Keep going
          index += size;
          return true;
        } else if (pos >= end) {
          // Done with the range
          return false;
        }

        // Correct the clip in case it does not
        if (start > pos) {
          clip.fLeft += run->positionX(start) - run->positionX(pos);
        }
        if (end < pos + size) {
          clip.fRight -= run->positionX(pos + size) - run->positionX(end);
        }

        clip.offset(line.offset());
        results.emplace_back(clip, run->leftToRight() ? SkTextDirection::ltr : SkTextDirection::rtl);

        // join does not work for empty rect (left == right)
        maxClip.fLeft   = SkMinScalar(maxClip.fLeft, clip.fLeft);
        maxClip.fTop    = SkMinScalar(maxClip.fTop, clip.fTop);
        maxClip.fRight  = SkMaxScalar(maxClip.fRight, clip.fRight);
        maxClip.fBottom = SkMaxScalar(maxClip.fBottom, clip.fBottom);

        index +=size;
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
          auto delta = (line.height() - rect.height()) / 2;
          rect.fTop = line.offset().fY + delta;
          rect.fBottom = line.offset().fY + line.height() - delta;

        } else if (rectHeightStyle == RectHeightStyle::kIncludeLineSpacingBottom) {
          rect.fBottom = line.offset().fY + line.height();
        }
      }
    } else {
      // Just leave the boxes the way they are
    }

    if (rectWidthStyle == RectWidthStyle::kMax) {
      for (auto& i = firstBox; i < results.size(); ++i) {
        auto clip = results[i].rect;
        auto dir = results[i].direction;
        if (clip.fLeft > maxClip.fLeft) {
          SkRect left = SkRect::MakeXYWH(0, clip.fTop, clip.fLeft - maxClip.fLeft, clip.fBottom);
          results.insert(results.begin() + i, { left, dir });
          ++i;
        }
        if (clip.fRight < maxClip.fRight) {
          SkRect right = SkRect::MakeXYWH(0, clip.fTop, maxClip.fRight - clip.fRight, clip.fBottom);
          results.insert(results.begin() + i, { right, dir });
          ++i;
        }
      }
    }
  }

  return results;
}

SkPositionWithAffinity SkParagraphImpl::getGlyphPositionAtCoordinate(SkScalar dx, SkScalar dy) {
  SkPositionWithAffinity result(0, Affinity::DOWNSTREAM);
  for (auto& line : fLines) {
    auto offsetY = line.offset().fY;
    auto advanceY = line.height();
    if (offsetY <= dy && dy < offsetY + advanceY) {
      // Find the line
      line.iterateThroughRuns(
          line.text(),
          false,
          [dx, &result]
          (SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded) {
            if (run->positionX(pos) <= dx && dx < run->positionX(pos + size)) {
              // Find the run
              size_t pos = 0;
              for (size_t i = 0; i < run->size(); ++i) {
                if (run->positionX(i) <= dx) {
                  // Find the position
                  pos = i;
                } else {
                  break;
                }
              }
              if (pos == 0) {
                result = { SkToS32(run->fClusterIndexes[0]), DOWNSTREAM };
              } else if (pos == run->size() - 1) {
                result = { SkToS32(run->fClusterIndexes.back()), UPSTREAM };
              } else {
                auto center = (run->positionX(pos + 1) + run->positionX(pos)) / 2;
                if ((dx <= center) == run->leftToRight()) {
                  result = { SkToS32(run->fClusterIndexes[pos]), DOWNSTREAM };
                } else {
                  result = { SkToS32(run->fClusterIndexes[pos + 1]), UPSTREAM };
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

  size_t currentPos = breaker.first();
  while (true) {
    auto start = currentPos;
    currentPos = breaker.next();
    if (breaker.eof()) {
      break;
    }
    if (start <= offset && currentPos > offset) {
      return {start, currentPos};
    }
  }
  return {0, 0};
}
