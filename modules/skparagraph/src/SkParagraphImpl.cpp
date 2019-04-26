/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <algorithm>
#include <stack>
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

  std::string toString(SkSpan<const char> text) {
    icu::UnicodeString
        utf16 = icu::UnicodeString(text.begin(), SkToS32(text.size()));
    std::string str;
    utf16.toUTF8String(str);
    return str;
  }

  void print(const SkCluster& cluster) {

    auto type = cluster.breakType() == SkCluster::BreakType::HardLineBreak
                ? "!"
                : (cluster.isSoftBreak() ? "?" : " ");
    SkDebugf("Cluster %s%s", type, cluster.isWhitespaces() ? "*" : " ");
    SkDebugf("[%d:%d) ", cluster.startPos(), cluster.endPos());

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
  }

  SkSpan<const char> operator*(const SkSpan<const char>& a, const SkSpan<const char>& b) {
    auto begin = SkTMax(a.begin(), b.begin());
    auto end = SkTMin(a.end(), b.end());
    return SkSpan<const char>(begin, end > begin ? end - begin : 0);
  }

  static inline SkUnichar utf8_next(const char** ptr, const char* end) {
    SkUnichar val = SkUTF::NextUTF8(ptr, end);
    if (val < 0) {
      return 0xFFFD;  // REPLACEMENT CHARACTER
    }
    return val;
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
    this->formatLinesByWords(fWidth);
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

  // Walk through all the run in the natural order
  std::vector<std::tuple<SkRun*, size_t, size_t>> toUpdate;
  for (auto& run : fRuns) {

    auto runStart = fClusters.size();
    // Walk through the glyph in the direction of input text
    run.iterateThroughClustersInTextOrder(
      [&run, this, &map](size_t glyphStart,
                         size_t glyphEnd,
                         size_t charStart,
                         size_t charEnd,
                         SkScalar width,
                         SkScalar height) {

        SkASSERT(charEnd >= charStart);
        SkSpan<const char>
            text(fUtf8.begin() + charStart, charEnd - charStart);

        auto& cluster = fClusters.emplace_back
            (&run, glyphStart, glyphEnd, text, width, height);
        // Mark line breaks
        auto found = map.find(cluster.text().end());
        if (found) {
          cluster.setBreakType(*found
                               ? SkCluster::BreakType::HardLineBreak
                               : SkCluster::BreakType::SoftLineBreak);
          cluster.setIsWhiteSpaces();
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

  fClusters.back().setBreakType(SkCluster::BreakType::HardLineBreak);
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

      auto& run = fParagraph->fRuns.emplace_back(fParagraph->text(), info, fParagraph->fRuns.count(), fAdvance.fX);
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

  SkSpan<SkBlock> styles(fTextStyles.begin(), fTextStyles.size());
  MultipleFontRunIterator font(fUtf8, styles, fFontCollection, fParagraphStyle.hintingIsOn());
  ShapeHandler handler(*this);
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
}

void SkParagraphImpl::formatLinesByWords(SkScalar maxWidth) {

  auto effectiveAlign = fParagraphStyle.effective_align();
  for (auto& line : fLines) {

    SkScalar delta = maxWidth - line.width();
    if (delta <= 0) {
      // Delta can be < 0 if there are extra whitespaces at the end of the line;
      // This is a limitation of a current version
      continue;
    }
    switch (effectiveAlign) {
      case SkTextAlign::left:

        line.shiftTo(0);
        break;
      case SkTextAlign::right:

        //line.setWidth(maxWidth);
        line.shiftTo(delta);
        break;
      case SkTextAlign::center: {

        //line.setWidth(maxWidth);
        line.shiftTo(delta / 2);
        break;
      }
      case SkTextAlign::justify: {

        if (&line != &fLines.back()) {
          line.justify(maxWidth);
        } else {
          line.shiftTo(0);
        }

        break;
      }
      default:
        break;
    }
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

SkLine& SkParagraphImpl::addLine (SkVector offset, SkVector advance, SkSpan<const char> text, SkRunMetrics sizes) {
  return fLines.emplace_back
      (offset,
       advance,
       SkSpan<SkCluster>(fClusters.begin(), fClusters.size()),
       text,
       sizes,
       true);
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
  for (auto& line : fLines) {
    auto intersect = line.text() * text;
    if (intersect.size() == 0) continue;

    auto firstBox = results.size();
    SkRect maxClip = SkRect::MakeXYWH(0, 0, 0, 0);
    line.iterateThroughRuns(
      intersect,
      false,
      [&results, &maxClip, &line]
      (SkRun* run, size_t pos, size_t size, SkRect clip, SkScalar shift, bool clippingNeeded) {
        clip.offset(line.offset());
        results.emplace_back(clip, run->leftToRight() ? SkTextDirection::ltr : SkTextDirection::rtl);
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
          rect.fBottom = line.offset().fY + line.height();

        } else if (rectHeightStyle == RectHeightStyle::kIncludeLineSpacingBottom) {
          rect.fBottom = line.offset().fY + line.height();
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
                result = { SkToS32(run->fClusterIndexes[0]), DOWNSTREAM };
              } else if (pos == run->size() - 1) {
                result = { SkToS32(run->fClusterIndexes.back()), UPSTREAM };
              } else {
                auto center = (run->position(pos + 1).fX + run->position(pos).fX) / 2;
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
