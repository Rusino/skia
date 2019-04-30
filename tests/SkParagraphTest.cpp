/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkParagraphBuilder.h"
#include "SkParagraphImpl.h"
#include "Resources.h"
#include "Test.h"

#define TestCanvasWidth 1000
#define TestCanvasHeight 600

namespace {
class TestFontStyleSet : public SkFontStyleSet {
 public:
  TestFontStyleSet() {}

  ~TestFontStyleSet() override {}

  void registerTypeface(sk_sp<SkTypeface> typeface) {

    _typeface = std::move(typeface);
  }

  int count() override {

    return 1;
  }

  void getStyle(int index, SkFontStyle* style, SkString* name) override {

    if (style != nullptr) { *style = _typeface->fontStyle(); }
    if (name != nullptr) { _typeface->getFamilyName(name); }
  }

  SkTypeface* createTypeface(int index) override {

    return SkRef(_typeface.get());
  }

  SkTypeface* matchStyle(const SkFontStyle& pattern) override {

    return SkRef(_typeface.get());
  }

 private:
  sk_sp<SkTypeface> _typeface;
};

class TestFontProvider : public SkFontMgr {
 public:
  TestFontProvider(sk_sp<SkTypeface> typeface) {

    RegisterTypeface(std::move(typeface));
  }
  ~TestFontProvider() override {}

  void RegisterTypeface(sk_sp<SkTypeface> typeface) {

    _set.registerTypeface(std::move(typeface));
    _set.getStyle(0, nullptr, &_familyName);
  }

  void
  RegisterTypeface(sk_sp<SkTypeface> typeface, std::string family_name_alias) {

    RegisterTypeface(std::move(typeface));
  }

  int onCountFamilies() const override {

    return 1;
  }

  void onGetFamilyName(int index, SkString* familyName) const override {

    *familyName = _familyName;
  }

  SkFontStyleSet* onMatchFamily(const char familyName[]) const override {

    if (std::strncmp(familyName, _familyName.c_str(), _familyName.size())
        == 0) {
      return (SkFontStyleSet*) &_set;
    }
    return nullptr;
  }

  SkFontStyleSet*
  onCreateStyleSet(int index) const override { return nullptr; }
  SkTypeface* onMatchFamilyStyle(const char familyName[],
                                 const SkFontStyle& style) const override { return nullptr; }
  SkTypeface* onMatchFamilyStyleCharacter(const char familyName[],
                                          const SkFontStyle& style,
                                          const char* bcp47[], int bcp47Count,
                                          SkUnichar character) const override { return nullptr; }
  SkTypeface* onMatchFaceStyle(const SkTypeface* tf,
                               const SkFontStyle& style) const override { return nullptr; }

  sk_sp<SkTypeface>
  onMakeFromData(sk_sp<SkData>, int ttcIndex) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset>,
                                          int ttcIndex) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset>,
                                         const SkFontArguments&) const override { return nullptr; }
  sk_sp<SkTypeface>
  onMakeFromFontData(std::unique_ptr<SkFontData>) const override { return nullptr; }
  sk_sp<SkTypeface>
  onMakeFromFile(const char path[],
                 int ttcIndex) const override { return nullptr; }

  sk_sp<SkTypeface> onLegacyMakeTypeface(const char familyName[],
                                         SkFontStyle style) const override { return nullptr; }

 private:
  TestFontStyleSet _set;
  SkString _familyName;
};
}

DEF_TEST(SkParagraph_GetWordBoundaries, reporter) {

  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface("fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  SkParagraphStyle paragraphStyle;
  paragraphStyle.setTextAlign(SkTextAlign::left);
  paragraphStyle.setMaxLines(10);
  paragraphStyle.turnHintingOff();
  SkTextStyle textStyle;
  textStyle.setFontFamily("Roboto");
  textStyle.setFontSize(52);
  textStyle.setLetterSpacing(1.19039);
  textStyle.setWordSpacing(5);
  textStyle.setHeight(1.5);
  textStyle.setColor(SK_ColorBLACK);
  textStyle.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));

  SkParagraphBuilder builder(paragraphStyle, fontCollection);
  builder.pushStyle(textStyle);
  builder.addText("12345  67890 12345 67890 12345 67890 12345 67890 12345 67890 12345 67890 12345");
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(550);

  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(0) == SkRange<size_t>(0, 5));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(1) == SkRange<size_t>(0, 5));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(2) == SkRange<size_t>(0, 5));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(3) == SkRange<size_t>(0, 5));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(4) == SkRange<size_t>(0, 5));

  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(5) == SkRange<size_t>(5, 7));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(6) == SkRange<size_t>(5, 7));

  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(7) == SkRange<size_t>(7, 12));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(8) == SkRange<size_t>(7, 12));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(9) == SkRange<size_t>(7, 12));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(10) == SkRange<size_t>(7, 12));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(11) == SkRange<size_t>(7, 12));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(12)  == SkRange<size_t>(12, 13));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(13)  == SkRange<size_t>(13, 18));
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(30) == SkRange<size_t>(30, 31));

  auto len = static_cast<SkParagraphImpl*>(paragraph.get())->text().size();
  REPORTER_ASSERT(reporter, paragraph->getWordBoundary(len - 1) == SkRange<size_t>(len - 5, len));
}

DEF_TEST(SkParagraph_GetRectsForRangeParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface("fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  SkParagraphStyle paragraphStyle;
  paragraphStyle.setTextAlign(SkTextAlign::left);
  paragraphStyle.setMaxLines(10);
  paragraphStyle.turnHintingOff();
  SkTextStyle textStyle;
  textStyle.setFontFamily("Roboto");
  textStyle.setFontSize(50);
  textStyle.setColor(SK_ColorBLACK);
  textStyle.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));

  SkParagraphBuilder builder(paragraphStyle, fontCollection);
  builder.pushStyle(textStyle);
  builder.addText("12345,  \"67890\" 12345 67890 12345 67890 12345 67890 12345 67890 12345 67890 12345");
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(550);

  RectHeightStyle heightStyle = RectHeightStyle::kMax;
  RectWidthStyle widthStyle = RectWidthStyle::kTight;
  SkScalar epsilon = 0.01;

  {
    auto result = paragraph->getRectsForRange(0, 0, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 0);
  }

  {
    auto result = paragraph->getRectsForRange(0, 1, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 1);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 0, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 0.40625, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 28.417969, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 59, epsilon));
  }

  {
    auto result = paragraph->getRectsForRange(2, 8, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 1);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 56.835938, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 0.40625, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 177.97266, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 59, epsilon));
  }

  {

    auto result = paragraph->getRectsForRange(8, 21, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 1);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 177.97266, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 0.40625, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 507.02344, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 59, epsilon));
  }

  {
    /*
     * There is only one box in SkParagraph for the entire text since we do not break
     * it by words. The function definition does not suggest that...
    auto result = paragraph->getRectsForRange(8, 21, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 4);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 211.375, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 59.40625, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 463.61719, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 118, epsilon));
    // TODO(garyq): The following set of vals are definetly wrong and
    // end of paragraph handling needs to be fixed in a later patch.
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[3].rect.left(), 0, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[3].rect.top(), 236.40625, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[3].rect.right(), 142.08984, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[3].rect.bottom(), 295, epsilon));
     */
  }

  {
    /*
     * There is a line break on position 21 (space). SkParagraph shows the box only for
     * [19:20] since the space in the end of the line is ignored.
    auto result = paragraph->getRectsForRange(19, 22, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 1);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 450.1875, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 0.40625, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 519.47266, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 59, epsilon));
    */
  }

  {
    auto result = paragraph->getRectsForRange(21, 21, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 0);
  }
}

DEF_TEST(SkParagraph_, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  REPORTER_ASSERT(reporter, true);
}

DEF_TEST(SkParagraph_GetRectsForRangeTight, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/NotoColorEmoji.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text =
      "(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)("
      "　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)("
      "　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)(　´･‿･｀)";
  auto icu_text = icu::UnicodeString::fromUTF8(text);
  std::u16string u16_text(icu_text.getBuffer(),
                          icu_text.getBuffer() + icu_text.length());

  SkParagraphStyle paragraphStyle;
  paragraphStyle.setTextAlign(SkTextAlign::left);
  paragraphStyle.setMaxLines(10);
  paragraphStyle.turnHintingOff();
  SkTextStyle textStyle;
  textStyle.setFontFamily("Noto Sans CJK JP");
  textStyle.setFontSize(50);
  textStyle.setColor(SK_ColorBLACK);
  textStyle.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));

  SkParagraphBuilder builder(paragraphStyle, fontCollection);
  builder.pushStyle(textStyle);
  builder.addText(u16_text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(550);

  RectHeightStyle heightStyle = RectHeightStyle::kTight;
  RectWidthStyle widthStyle = RectWidthStyle::kTight;
  SkScalar epsilon = 0.01;

  {
    auto result = paragraph->getRectsForRange(0, 0, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 0);
  }

  {
    auto result = paragraph->getRectsForRange(0, 1, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 1);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 0, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 0, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 16.898438, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 74, epsilon));
  }
// Minikin uses start and end parameters of getRectsForRange as a glyph positions.
// Does it make sense?
  {
    auto result = paragraph->getRectsForRange(2, 8, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 1);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 66.899, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 0, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 264.09375, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 74, epsilon));
  }

  {
    auto result = paragraph->getRectsForRange(8, 21, heightStyle, widthStyle);
    REPORTER_ASSERT(reporter, result.size() == 2);
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.left(), 264.09375, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.top(), 0, epsilon));
    // It seems that Minikin does not take in account like breaks, but we do.
    // SkParagraph returns 528.199 instead
    //REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.right(), 595.08594, epsilon));
    REPORTER_ASSERT(reporter, SkScalarNearlyEqual(result[0].rect.bottom(), 74, epsilon));
  }
}

DEF_TEST(SkParagraph_GetGlyphPositionAtCoordinateParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  SkParagraphStyle paragraphStyle;
  paragraphStyle.setTextAlign(SkTextAlign::left);
  paragraphStyle.setMaxLines(10);
  paragraphStyle.turnHintingOff();
  SkTextStyle textStyle;
  textStyle.setFontFamily("Roboto");
  textStyle.setFontSize(50);
  textStyle.setLetterSpacing(1);
  textStyle.setWordSpacing(5);
  textStyle.setHeight(1);
  textStyle.setColor(SK_ColorBLACK);
  textStyle.setFontStyle(SkFontStyle(SkFontStyle::kMedium_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));

  SkParagraphBuilder builder(paragraphStyle, fontCollection);
  builder.pushStyle(textStyle);
  builder.addText("12345  67890 12345 67890 12345 67890 12345 67890 12345 67890 12345 67890 12345");
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(550);

  // Tests for getGlyphPositionAtCoordinate()
  // NOTE: resulting values can be a few off from their respective positions in
  // the original text because the final trailing whitespaces are sometimes not
  // drawn (namely, when using "justify" alignment) and therefore are not active
  // glyphs.
  // TODO: letter_spacing and word_spacing are not implemented yet
  //  so the numbers are off...
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(-10000, -10000).position == 0);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(-1, -1).position == 0);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(0, 0).position == 0);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(3, 3).position == 0);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(35, 1).position == 1);
  /*
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(300, 2).position == 11);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(301, 2.2).position == 11);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(302, 2.6).position == 11);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(301, 2.1).position == 11);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(100000, 20).position == 18);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(450, 20).position == 16);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(100000, 90).position == 36);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(-100000, 90).position == 18);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(20, -80).position == 1);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(1, 90).position == 18);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(1, 170).position == 36);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(10000, 180).position == 72);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(70, 180).position == 56);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(1, 270).position == 72);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(35, 90).position == 19);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(10000, 10000).position == 77);
  REPORTER_ASSERT(reporter, paragraph->getGlyphPositionAtCoordinate(85, 10000).position == 75);
   */
}

DEF_TEST(SkParagraph_DefaultStyleParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text = "No TextStyle! Uh Oh!";

  SkParagraphStyle paragraph_style;
  paragraph_style.getTextStyle().setColor(SK_ColorBLUE);
  paragraph_style.getTextStyle().setFontFamilies(std::vector<std::string>(1, "Roboto"));
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);
  builder.addText(text);

  auto paragraph = builder.Build();
  paragraph->layout(500);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());

  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1);
}

DEF_TEST(SkParagraph_SimpleParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text = "Hello World Text Dialog";

  SkParagraphStyle paragraph_style;
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setColor(SK_ColorBLACK);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(500);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());

  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1); // paragraph style does not count
  REPORTER_ASSERT(reporter, impl->styles()[0].style().equals(text_style));
}

DEF_TEST(SkParagraph_BoldParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text = "This is Red max bold text!";
  auto icu_text = icu::UnicodeString::fromUTF8(text);
  std::u16string u16_text(icu_text.getBuffer(),
                          icu_text.getBuffer() + icu_text.length());

  SkParagraphStyle paragraph_style;
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setColor(SK_ColorRED);
  text_style.setFontSize(60);
  text_style.setLetterSpacing(0);
  text_style.setFontStyle(
      SkFontStyle(SkFontStyle::kBlack_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(1000);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());

  REPORTER_ASSERT(reporter, impl->text().size() == std::string{text}.length());
  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles()[0].style().equals(text_style));
}

DEF_TEST(SkParagraph_LongWordParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text =
      "A "
      "veryverylongwordtoseewherethiswillwraporifitwillatallandifitdoesthenthat"
      "wouldbeagoodthingbecausethebreakingisworking.";

  SkParagraphStyle paragraph_style;
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setColor(SK_ColorRED);
  text_style.setFontSize(31);
  text_style.setLetterSpacing(0);
  text_style.setWordSpacing(0);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(1);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth / 2);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  REPORTER_ASSERT(reporter, impl->text().size() == std::string{text}.length());
  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles()[0].style().equals(text_style));
  // TODO: Improve line breaking algorightm for too long words
  REPORTER_ASSERT(reporter, impl->lines().size() == 5);
}

DEF_TEST(SkParagraph_NewlineParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text = "line1\nline2 test1 test2 test3 test4 test5 test6 test7\nline3\n\nline4 "
                     "test1 test2 test3 test4";
  SkParagraphStyle paragraph_style;
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setColor(SK_ColorRED);
  text_style.setFontSize(60);
  text_style.setLetterSpacing(0);
  text_style.setWordSpacing(0);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(1);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth - 300);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  // Minikin does not count empty lines but SkParagraph does
  REPORTER_ASSERT(reporter, impl->lines().size() == 7);

  REPORTER_ASSERT(reporter, impl->lines()[0].offset().fY == 0);
  REPORTER_ASSERT(reporter, impl->lines()[1].offset().fY == 70);
  REPORTER_ASSERT(reporter, impl->lines()[2].offset().fY == 140);
  REPORTER_ASSERT(reporter, impl->lines()[3].offset().fY == 210);
  REPORTER_ASSERT(reporter, impl->lines()[4].offset().fY == 280); // Empty line
  REPORTER_ASSERT(reporter, impl->lines()[5].offset().fY == 350);
  REPORTER_ASSERT(reporter, impl->lines()[6].offset().fY == 420);

  SkScalar epsilon = 0.1;
  REPORTER_ASSERT(reporter,  SkScalarNearlyEqual(impl->lines()[0].width(), 130.31, epsilon));
  REPORTER_ASSERT(reporter,  SkScalarNearlyEqual(impl->lines()[1].width(), 586.64, epsilon));
  REPORTER_ASSERT(reporter,  SkScalarNearlyEqual(impl->lines()[2].width(), 593.496, epsilon));
  REPORTER_ASSERT(reporter,  SkScalarNearlyEqual(impl->lines()[3].width(), 130.31, epsilon));
  REPORTER_ASSERT(reporter,  SkScalarNearlyEqual(impl->lines()[4].width(), 0, epsilon)); // Empty line
  REPORTER_ASSERT(reporter,  SkScalarNearlyEqual(impl->lines()[5].width(), 586.64, epsilon));
  REPORTER_ASSERT(reporter,  SkScalarNearlyEqual(impl->lines()[6].width(), 137.167, epsilon));

  REPORTER_ASSERT(reporter, impl->lines()[0].shift() == 0);
}

DEF_TEST(SkParagraph_LeftAlignParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text =
      "This is a very long sentence to test if the text will properly wrap "
      "around and go to the next line. Sometimes, short sentence. Longer "
      "sentences are okay too because they are nessecary. Very short. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum.";

  SkParagraphStyle paragraph_style;
  paragraph_style.setMaxLines(14);
  paragraph_style.setTextAlign(SkTextAlign::left);
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setFontSize(26);
  text_style.setLetterSpacing(1);
  text_style.setWordSpacing(5);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(1);
  text_style.setDecoration(SkTextDecoration::kUnderline);
  text_style.setDecorationColor(SK_ColorBLACK);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth - 100);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  // Some of the formatting lazily done on paint
  impl->formatLinesByWords(TestCanvasWidth - 100);

  REPORTER_ASSERT(reporter, impl->text().size() == std::string{text}.length());
  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles()[0].style().equals(text_style));
  REPORTER_ASSERT(reporter, impl->lines().size() == paragraph_style.getMaxLines());

  double expected_y = 0;
  REPORTER_ASSERT(reporter, impl->lines()[0].offset() == SkPoint::Make(0, expected_y));
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[1].offset() == SkPoint::Make(0, expected_y));
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[2].offset() == SkPoint::Make(0, expected_y));
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[3].offset() == SkPoint::Make(0, expected_y));
  expected_y += 30 * 10;
  REPORTER_ASSERT(reporter, impl->lines()[13].offset() == SkPoint::Make(0, expected_y));

  REPORTER_ASSERT(reporter, paragraph_style.getTextAlign() == impl->paragraphStyle().getTextAlign());

  // Tests for GetGlyphPositionAtCoordinate()
  // TODO: implement word_spacing and letter_spacing
  REPORTER_ASSERT(reporter, impl->getGlyphPositionAtCoordinate(0, 0).position == 0);
  REPORTER_ASSERT(reporter, impl->getGlyphPositionAtCoordinate(1, 1).position == 0);
  //REPORTER_ASSERT(reporter, impl->getGlyphPositionAtCoordinate(1, 35).position == 68);
  //REPORTER_ASSERT(reporter, impl->getGlyphPositionAtCoordinate(1, 70).position == 134);
  //REPORTER_ASSERT(reporter, impl->getGlyphPositionAtCoordinate(2000, 35).position == 134);
}

DEF_TEST(SkParagraph_RightAlignParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text =
      "This is a very long sentence to test if the text will properly wrap "
      "around and go to the next line. Sometimes, short sentence. Longer "
      "sentences are okay too because they are nessecary. Very short. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum.";

  SkParagraphStyle paragraph_style;
  paragraph_style.setMaxLines(14);
  paragraph_style.setTextAlign(SkTextAlign::right);
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setFontSize(26);
  text_style.setLetterSpacing(1);
  text_style.setWordSpacing(5);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(1);
  text_style.setDecoration(SkTextDecoration::kUnderline);
  text_style.setDecorationColor(SK_ColorBLACK);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth - 100);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  // Some of the formatting lazily done on paint
  impl->formatLinesByWords(TestCanvasWidth - 100);

  REPORTER_ASSERT(reporter, impl->text().size() == std::string{text}.length());
  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles()[0].style().equals(text_style));
  // Minikin has two records for each due to 'ghost' trailing whitespace run, SkParagraph - 1
  REPORTER_ASSERT(reporter, impl->lines().size() == paragraph_style.getMaxLines());

  // Minikin has initial offset 24???
  double expected_y = 0;
  REPORTER_ASSERT(reporter, impl->lines()[0].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[1].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[2].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[3].offset().fY == expected_y);
  expected_y += 30 * 10;
  REPORTER_ASSERT(reporter, impl->lines()[13].offset().fY == expected_y);

  auto calculate = [](const SkLine& line) -> SkScalar {
    return TestCanvasWidth - 100 - line.offset().fX - line.width();
  };
  
  SkScalar epsilon = 0.1;
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[0]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[1]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[2]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[3]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[13]), 0, epsilon));

  REPORTER_ASSERT(reporter, paragraph_style.getTextAlign() == impl->paragraphStyle().getTextAlign());
}

DEF_TEST(SkParagraph_CenterAlignParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text =
      "This is a very long sentence to test if the text will properly wrap "
      "around and go to the next line. Sometimes, short sentence. Longer "
      "sentences are okay too because they are nessecary. Very short. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum.";

  SkParagraphStyle paragraph_style;
  paragraph_style.setMaxLines(14);
  paragraph_style.setTextAlign(SkTextAlign::center);
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setFontSize(26);
  text_style.setLetterSpacing(1);
  text_style.setWordSpacing(5);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(1);
  text_style.setDecoration(SkTextDecoration::kUnderline);
  text_style.setDecorationColor(SK_ColorBLACK);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth - 100);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  // Some of the formatting lazily done on paint
  impl->formatLinesByWords(TestCanvasWidth - 100);

  REPORTER_ASSERT(reporter, impl->text().size() == std::string{text}.length());
  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles()[0].style().equals(text_style));
  // Minikin has two records for each due to 'ghost' trailing whitespace run, SkParagraph - 1
  REPORTER_ASSERT(reporter, impl->lines().size() == paragraph_style.getMaxLines());

  // Minikin has initial offset 24???
  double expected_y = 0;
  REPORTER_ASSERT(reporter, impl->lines()[0].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[1].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[2].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[3].offset().fY == expected_y);
  expected_y += 30 * 10;
  REPORTER_ASSERT(reporter, impl->lines()[13].offset().fY == expected_y);

  auto calculate = [](const SkLine& line) -> SkScalar {
    return TestCanvasWidth - 100 - (line.offset().fX * 2 + line.width());
  };

  SkScalar epsilon = 0.1;
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[0]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[1]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[2]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[3]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[13]), 0, epsilon));

  REPORTER_ASSERT(reporter, paragraph_style.getTextAlign() == impl->paragraphStyle().getTextAlign());
}

DEF_TEST(SkParagraph_JustifyAlignParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text =
      "This is a very long sentence to test if the text will properly wrap "
      "around and go to the next line. Sometimes, short sentence. Longer "
      "sentences are okay too because they are nessecary. Very short. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum. "
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
      "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
      "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
      "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
      "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
      "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
      "mollit anim id est laborum.";

  SkParagraphStyle paragraph_style;
  paragraph_style.setMaxLines(14);
  paragraph_style.setTextAlign(SkTextAlign::justify);
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setFontSize(26);
  text_style.setLetterSpacing(1);
  text_style.setWordSpacing(5);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(1);
  text_style.setDecoration(SkTextDecoration::kUnderline);
  text_style.setDecorationColor(SK_ColorBLACK);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth - 100);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  // Some of the formatting lazily done on paint
  impl->formatLinesByWords(TestCanvasWidth - 100);

  REPORTER_ASSERT(reporter, impl->text().size() == std::string{text}.length());
  REPORTER_ASSERT(reporter, impl->runs().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles().size() == 1);
  REPORTER_ASSERT(reporter, impl->styles()[0].style().equals(text_style));
  // Minikin has two records for each due to 'ghost' trailing whitespace run, SkParagraph - 1
  REPORTER_ASSERT(reporter, impl->lines().size() == paragraph_style.getMaxLines());

  // Minikin has initial offset 24???
  double expected_y = 0;
  REPORTER_ASSERT(reporter, impl->lines()[0].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[1].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[2].offset().fY == expected_y);
  expected_y += 30;
  REPORTER_ASSERT(reporter, impl->lines()[3].offset().fY == expected_y);
  expected_y += 30 * 10;
  REPORTER_ASSERT(reporter, impl->lines()[13].offset().fY == expected_y);

  auto calculate = [](const SkLine& line) -> SkScalar {
    return TestCanvasWidth - 100 - (line.offset().fX  + line.width());
  };

  SkScalar epsilon = 0.1;
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[0]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[1]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[2]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[3]), 0, epsilon));
  REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(impl->lines()[12]), 0, epsilon));

  REPORTER_ASSERT(reporter, paragraph_style.getTextAlign() == impl->paragraphStyle().getTextAlign());
}

DEF_TEST(SkParagraph_JustifyRTL, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/ahem.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  const char* text =
      "אאא בּבּבּבּ אאאא בּבּ אאא בּבּבּ אאאאא בּבּבּבּ אאאא בּבּבּבּבּ "
      "אאאאא בּבּבּבּבּ אאאבּבּבּבּבּבּאאאאא בּבּבּבּבּבּאאאאאבּבּבּבּבּבּ אאאאא בּבּבּבּבּ "
      "אאאאא בּבּבּבּבּבּ אאאאא בּבּבּבּבּבּ אאאאא בּבּבּבּבּבּ אאאאא בּבּבּבּבּבּ אאאאא בּבּבּבּבּבּ";

  SkParagraphStyle paragraph_style;
  paragraph_style.setMaxLines(14);
  paragraph_style.setTextAlign(SkTextAlign::justify);
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Ahem"));
  text_style.setFontSize(26);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(1);
  text_style.setDecoration(SkTextDecoration::kUnderline);
  text_style.setDecorationColor(SK_ColorBLACK);
  builder.pushStyle(text_style);
  builder.addText(text);
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth - 100);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  // Some of the formatting lazily done on paint
  impl->formatLinesByWords(TestCanvasWidth - 100);

  auto calculate = [](const SkLine& line) -> SkScalar {
    return TestCanvasWidth - 100 - (line.offset().fX  + line.width());
  };

  SkScalar epsilon = 0.1;
  for (auto& line : impl->lines()) {
    if (&line == impl->lines().end() - 1) {
      REPORTER_ASSERT(reporter, calculate(line) > epsilon);
    } else {
      REPORTER_ASSERT(reporter, SkScalarNearlyEqual(calculate(line), 0, epsilon));
    }
  }

  // Just make sure the the text is actually RTL
  for (auto& run : impl->runs()) {
    REPORTER_ASSERT(reporter, !run.leftToRight());
  }
}

DEF_TEST(SkParagraph_DecorationsParagraph, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

/*
 *   void scanStyles(SkStyleType style, SkSpan<SkBlock> blocks,
                  std::function<void(SkTextStyle, SkScalar)> apply)
 */

  SkParagraphStyle paragraph_style;
  paragraph_style.setMaxLines(14);
  paragraph_style.setTextAlign(SkTextAlign::left);
  paragraph_style.turnHintingOff();
  SkParagraphBuilder builder(paragraph_style, fontCollection);

  SkTextStyle text_style;
  text_style.setFontFamilies(std::vector<std::string>(1, "Roboto"));
  text_style.setFontSize(26);
  text_style.setLetterSpacing(0);
  text_style.setWordSpacing(5);
  text_style.setColor(SK_ColorBLACK);
  text_style.setHeight(2);
  text_style.setDecoration(SkTextDecoration::kUnderline);
  text_style.setDecorationColor(SK_ColorBLACK);
  text_style.setDecoration((SkTextDecoration)(SkTextDecoration::kUnderline |
                                              SkTextDecoration::kOverline |
                                              SkTextDecoration::kLineThrough));
  text_style.setDecorationStyle(SkTextDecorationStyle::kSolid);
  text_style.setDecorationColor(SK_ColorBLACK);
  text_style.setDecorationThicknessMultiplier(2.0);
  builder.pushStyle(text_style);
  builder.addText("This text should be");

  text_style.setDecorationStyle(SkTextDecorationStyle::kDouble);
  text_style.setDecorationColor(SK_ColorBLUE);
  text_style.setDecorationThicknessMultiplier(1.0);
  builder.pushStyle(text_style);
  builder.addText(" decorated even when");

  text_style.setDecorationStyle(SkTextDecorationStyle::kDotted);
  text_style.setDecorationColor(SK_ColorBLACK);
  builder.pushStyle(text_style);
  builder.addText(" wrapped around to");

  text_style.setDecorationStyle(SkTextDecorationStyle::kDashed);
  text_style.setDecorationColor(SK_ColorBLACK);
  text_style.setDecorationThicknessMultiplier(3.0);
  builder.pushStyle(text_style);
  builder.addText(" the next line.");

  text_style.setDecorationStyle(SkTextDecorationStyle::kWavy);
  text_style.setDecorationColor(SK_ColorRED);
  text_style.setDecorationThicknessMultiplier(1.0);
  builder.pushStyle(text_style);
  builder.addText(" Otherwise, bad things happen.");
  builder.pop();

  auto paragraph = builder.Build();
  paragraph->layout(TestCanvasWidth - 100);

  auto impl = static_cast<SkParagraphImpl*>(paragraph.get());
  // Some of the formatting lazily done on paint
  impl->formatLinesByWords(TestCanvasWidth - 100);

  size_t index = 0;
  for (auto& line : impl->lines()) {
    line.scanStyles(SkStyleType::Decorations, impl->styles(),
        [&index, reporter](SkTextStyle style, SkScalar offsetX) {
          auto decoration = (SkTextDecoration)(SkTextDecoration::kUnderline |
              SkTextDecoration::kOverline |
              SkTextDecoration::kLineThrough);
      REPORTER_ASSERT(reporter, style.getDecoration() == decoration);
      switch (index) {
        case 0:
          REPORTER_ASSERT(reporter, style.getDecorationStyle() == SkTextDecorationStyle::kSolid);
          REPORTER_ASSERT(reporter, style.getDecorationColor() == SK_ColorBLACK);
          REPORTER_ASSERT(reporter, style.getDecorationThicknessMultiplier() == 2.0);
          break;
        case 1:
          REPORTER_ASSERT(reporter, style.getDecorationStyle() == SkTextDecorationStyle::kDouble);
          REPORTER_ASSERT(reporter, style.getDecorationColor() == SK_ColorBLUE);
          REPORTER_ASSERT(reporter, style.getDecorationThicknessMultiplier() == 1.0);
          break;
        case 2:
          REPORTER_ASSERT(reporter, style.getDecorationStyle() == SkTextDecorationStyle::kDotted);
          REPORTER_ASSERT(reporter, style.getDecorationColor() == SK_ColorBLACK);
          REPORTER_ASSERT(reporter, style.getDecorationThicknessMultiplier() == 1.0);
          break;
        case 3:
          REPORTER_ASSERT(reporter, style.getDecorationStyle() == SkTextDecorationStyle::kDashed);
          REPORTER_ASSERT(reporter, style.getDecorationColor() == SK_ColorBLACK);
          REPORTER_ASSERT(reporter, style.getDecorationThicknessMultiplier() == 3.0);
          break;
        case 4:
          REPORTER_ASSERT(reporter, style.getDecorationStyle() == SkTextDecorationStyle::kWavy);
          REPORTER_ASSERT(reporter, style.getDecorationColor() == SK_ColorRED);
          REPORTER_ASSERT(reporter, style.getDecorationThicknessMultiplier() == 1.0);
          break;
      }
      ++index;
    });
  }
}

DEF_TEST(SkParagraph_2, reporter) {
  sk_sp<SkFontCollection> fontCollection = sk_make_sp<SkFontCollection>();
  sk_sp<TestFontProvider> testFontProvider =
      sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
          "fonts/Roboto-Medium.ttf"));
  fontCollection->setTestFontManager(testFontProvider);

  REPORTER_ASSERT(reporter, true);
}