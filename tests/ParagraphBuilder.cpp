/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkPaint.h"
#include "SkPoint.h"
#include "SkSerialProcs.h"
#include "SkTextBlobPriv.h"
#include "SkTo.h"
#include "SkTypeface.h"

#include "SkTestTypeface.h"

#include "unicode/utypes.h"
#include "unicode/unistr.h"
#include "SkParagraphBuilder.h"
#include "SkParagraph.h"
#include "SkFontStyle.h"

#include "Test.h"
#include "sk_tool_utils.h"

#include "test_font_monospace.inc"
#include "test_font_sans_serif.inc"
#include "test_font_serif.inc"
#include "test_font_index.inc"

class TestFontStyleSet : public SkFontStyleSet {
 public:
  TestFontStyleSet(const char* familyName) : _familyName(familyName) {}

  int count() override { return _entries.size(); }

  void getStyle(int index, SkFontStyle* style, SkString* name) override {
    if (style) { *style = _entries[index]._fontStyle; }
    if (name) { *name = _familyName; }
  }

  SkTypeface* createTypeface(int index) override {
    return SkRef(_entries[index]._typeface.get());
  }

  SkTypeface* matchStyle(const SkFontStyle& pattern) override {
    return this->matchStyleCSS3(pattern);
  }

  SkString getFamilyName() { return _familyName; }

  struct Entry {
    Entry(sk_sp<SkTypeface> typeface, SkFontStyle style)
    : _typeface(std::move(typeface))
    , _fontStyle(style)
    {}
    sk_sp<SkTypeface> _typeface;
    SkFontStyle _fontStyle;
  };

  std::vector<Entry> _entries;
  SkString _familyName;
};

class TestFontManager : public SkFontMgr {
 public:
  TestFontManager(const char* familyName) {

    if (_set == nullptr) {
      _set = sk_make_sp<TestFontStyleSet>(familyName);
    }
    for (const auto& sub : gSubFonts) {
      if (strcmp(sub.fFamilyName, familyName) == 0) {
        sk_sp<SkTestTypeface> typeface = sk_make_sp<SkTestTypeface>(sk_make_sp<SkTestFont>(sub.fFont), sub.fStyle);
        _set->_entries.emplace_back(std::move(typeface), sub.fStyle);
      }
    }
  }

  int onCountFamilies() const override { return 1; }

  void onGetFamilyName(int index, SkString* familyName) const override {
    *familyName = _set->getFamilyName();
  }

  SkFontStyleSet* onCreateStyleSet(int index) const override {
    sk_sp<SkFontStyleSet> ref = _set;
    return ref.release();
  }

  SkFontStyleSet* onMatchFamily(const char familyName[]) const override {
    if (strstr(familyName, _set->getFamilyName().c_str())) {
      return _set.get();
    }
    return nullptr;
  }

  SkTypeface* onMatchFamilyStyle(const char familyName[],
                                 const SkFontStyle& style) const override {
    sk_sp<SkFontStyleSet> styleSet(this->matchFamily(familyName));
    return styleSet->matchStyle(style);
  }

  SkTypeface* onMatchFamilyStyleCharacter(const char familyName[],
                                          const SkFontStyle& style,
                                          const char* bcp47[], int bcp47Count,
                                          SkUnichar character) const override {
    (void)bcp47;
    (void)bcp47Count;
    (void)character;
    return this->matchFamilyStyle(familyName, style);
  }

  SkTypeface* onMatchFaceStyle(const SkTypeface* tf,
                               const SkFontStyle& style) const override {
    SkString familyName;
    tf->getFamilyName(&familyName);
    return this->matchFamilyStyle(familyName.c_str(), style);
  }

  sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData>, int ttcIndex) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset>,
                                          int ttcIndex) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset>,
                                         const SkFontArguments&) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromFontData(std::unique_ptr<SkFontData>) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromFile(const char path[], int ttcIndex) const override { return nullptr; }

  sk_sp<SkTypeface> onLegacyMakeTypeface(const char familyName[],
                                         SkFontStyle style) const override { return nullptr; }

 private:
  sk_sp<TestFontStyleSet> _set;
};

class ParagraphBuilderTester {
 public:
  // This unit test feeds an ParagraphBuilderTester various commands then checks to see if
  // the result contains the provided data.
  static void TestParagraphBuilder(skiatest::Reporter* reporter) {

    SkParagraphStyle ps;
    SkTextStyle& ts = ps.getTextStyle();

    SkTextStyle ts1;
    ts1.setFontSize(10);
    ts1.setFontFamily("Arial");
    ts1.setBackgroundColor(SK_ColorYELLOW);

    SkTextStyle ts2;
    ts2.setFontSize(20);
    ts2.setFontFamily("Arial");
    ts2.setBackgroundColor(SK_ColorBLUE);

    SkTextStyle ts3;
    ts3.setFontSize(30);
    ts3.setFontFamily("Arial");
    ts3.setBackgroundColor(SK_ColorLTGRAY);

    SkTextStyle ts4;
    ts3.setFontSize(40);
    ts3.setFontFamily("Arial");
    ts3.setBackgroundColor(SK_ColorLTGRAY);

    // Empty run set (will get default values
    std::vector<RunDef> input0 = {
        RunDef(ps)
    };
    RunBuilderTest(reporter, input0, "", {});

    // Simple paragraph
    std::vector<RunDef> input1 = {
        RunDef(ps),
        RunDef(ts1),
        RunDef("Simple paragraph.", false),
    };
    std::vector<StyledText> output1 = {
        {0, 17, ts1},
    };
    RunBuilderTest(reporter, input1, "Simple paragraph.", output1);

    // Simple full coverage (list of level-1 styles)
    std::vector<RunDef> input2 = {
        RunDef(ps),
        RunDef(ts1),
        RunDef("Style #01 "),
        RunDef(),
        RunDef(ts2),
        RunDef("Style #02 "),
        RunDef(),
        RunDef(ts3),
        RunDef("Style #03 "),
        RunDef(),
    };
    std::vector<StyledText> output2 = {
        {00, 10, ts1},
        {10, 20, ts2},
        {20, 30, ts3},
    };
    RunBuilderTest(reporter, input2, "Style #01 Style #02 Style #03 ", output2);

    // Few blocks with the same text style come out as one merged block
    std::vector<RunDef> input3 = {
        RunDef(ps),
        RunDef(ts1),
        RunDef("Style #01 "),
        RunDef(),
        RunDef(ts1),
        RunDef("Style #02 "),
        RunDef(),
        RunDef(ts1),
        RunDef("Style #03 "),
        RunDef(),
    };
    std::vector<StyledText> output3 = {
        {00, 30, ts1},
    };
    RunBuilderTest(reporter, input3, "Style #01 Style #02 Style #03 ", output3);

    // Few small blocks and the rest as paragraph
    std::vector<RunDef> input4 = {
        RunDef(ps),
        RunDef(ts1),
        RunDef("Style #01 "),
        RunDef(),
        RunDef("#01a      "),
        RunDef(ts2),
        RunDef("Style #02 "),
        RunDef(),
        RunDef("#02a      "),
        RunDef(ts3),
        RunDef("Style #03 "),
        RunDef(),
        RunDef("#03a      "),
    };
    std::vector<StyledText> output4 = {
        {00, 10, ts1},
        {10, 20, ts},
        {20, 30, ts2},
        {30, 40, ts},
        {40, 50, ts3},
        {50, 60, ts},
    };
    RunBuilderTest(reporter,
                   input4,
                   "Style #01 #01a      Style #02 #02a      Style #03 #03a      ",
                   output4);

    // Multi-level hierarchy of styles
    std::vector<RunDef> input5 = {
        RunDef(ps),
        RunDef(ts1),
        RunDef("111a "),
        RunDef(ts2),
        RunDef("222a "),
        RunDef(),
        RunDef("111b "),
        RunDef(ts2),
        RunDef("222b "),
        RunDef(ts3),
        RunDef("333  "),
        RunDef(ts4),
        RunDef("444  "),
    };
    std::vector<StyledText> output5 = {
        {00, 05, ts1},
        {05, 10, ts2},
        {10, 15, ts1},
        {15, 20, ts2},
        {20, 25, ts3},
        {25, 30, ts4},
    };
    RunBuilderTest(reporter, input5, "111a 222a 111b 222b 333  444  ", output5);

    // Too many pops
    std::vector<RunDef> input6 = {
        RunDef(ps),
        RunDef(ts1),
        RunDef(),
        RunDef(),
        RunDef("Simple paragraph."),
    };
    std::vector<StyledText> output6 = {
        {0, 17, ts},
    };
    RunBuilderTest(reporter, input6, "Simple paragraph.", output6);
  }

  // This unit test loads and uses different fonts to check if they are available
  static void TestFontCollection(skiatest::Reporter* reporter) {

    const SkFontStyle italic(SkFontStyle::kNormal_Weight, SkFontStyle::kNormal_Width, SkFontStyle::Slant::kItalic_Slant);
    const SkFontStyle normal(SkFontStyle::kNormal_Weight, SkFontStyle::kNormal_Width, SkFontStyle::Slant::kUpright_Slant);
    const SkFontStyle bold(SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::Slant::kUpright_Slant);
    const SkFontStyle italic_bold(SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::Slant::kItalic_Slant);

    SkFontCollection fontCollection;
    fontCollection.DisableFontFallback();

    sk_sp<TestFontManager> assetFontManager = sk_make_sp<TestFontManager>("monospace");
    sk_sp<TestFontManager> dynamicFontManager = sk_make_sp<TestFontManager>("sans-serif");
    sk_sp<TestFontManager> testFontManager = sk_make_sp<TestFontManager>("serif");

    // No fonts there yet
    RunFontTest(reporter, fontCollection, "monospace", italic, false);
    fontCollection.SetAssetFontManager(assetFontManager);
    RunFontTest(reporter, fontCollection, "monospace", italic, true);
    RunFontTest(reporter, fontCollection, "monospace", bold, true);

    // No fonts from dynamic font provider
    RunFontTest(reporter, fontCollection, "sans-serif", italic_bold, false);
    fontCollection.SetDynamicFontManager(dynamicFontManager);
    RunFontTest(reporter, fontCollection, "sans-serif", italic_bold, true);

    // No fonts from test font provider
    RunFontTest(reporter, fontCollection, "serif", normal, false);
    fontCollection.SetTestFontManager(testFontManager);
    RunFontTest(reporter, fontCollection, "serif", normal, true);

    // This font is not anywhere
    RunFontTest(reporter, fontCollection, "Something else", normal, false);
  }

 private:

  enum Command { add, add1, push, pop, paragraph };

  struct RunDef {
    RunDef(const std::string& text, bool asString = true)
      : command(asString ? add : add1), text(text) {}
    RunDef(SkParagraphStyle ps) : command(paragraph), paragraphStyle(ps) {}
    RunDef(SkTextStyle ts) : command(push), textStyle(ts) {}
    RunDef() : command(pop) {}
    Command command;
    std::string text;
    SkParagraphStyle paragraphStyle;
    SkTextStyle textStyle;
  };

  static void RunBuilderTest(skiatest::Reporter* reporter,
                             const std::vector<RunDef>& commands,
                             const std::string& text,
                             std::vector<StyledText> runs) {
    if (!commands.empty()) {
      REPORTER_ASSERT(reporter,
                      commands.size() > 0 && commands[0].command == paragraph);
    }

    SkParagraphStyle ps(commands[0].paragraphStyle);;

    SkParagraphBuilder builder(ps, std::make_shared<SkFontCollection>());
    for (size_t i = 1; i < commands.size(); ++i) {
      auto& command = commands[i];
      switch (command.command) {
        case add:
          builder.AddText(command.text);
          break;
        case add1:
          builder.AddText(command.text.data());
          break;
        case push:
          builder.PushStyle(command.textStyle);
          break;
        case pop:
          builder.Pop();
          break;
        default:
          REPORT_FAILURE(reporter,
                         "Wrong command for SkParagraphBuilder.",
                         SkString());
      }
    }

    // To imitate the "Build" call:
    builder.EndRunIfNeeded();

    if (builder._text.empty()) {
      REPORTER_ASSERT(reporter, text.empty());
    } else {
      icu::UnicodeString
          utf16 = icu::UnicodeString(&builder._text[0], builder._text.size());
      std::string str;
      utf16.toUTF8String(str);
      if (str != text) {
        SkDebugf("'%s' != '%s'\n", str.c_str(), text.c_str());
      }
      REPORTER_ASSERT(reporter, str == text);
    }

    if (builder._runs != runs) {
      SkDebugf("runs: %d != %d\n", builder._runs.size(), runs.size());
    }
    REPORTER_ASSERT(reporter, builder._runs == runs);
  }

  static void RunFontTest(skiatest::Reporter* reporter,
                          SkFontCollection& fontCollection,
                          const char* familyName,
                          SkFontStyle fontStyle,
                          bool mustBeFound) {

    SkTextStyle textStyle;
    textStyle.setFontFamily(familyName);
    textStyle.setFontStyle(fontStyle);

    auto found = fontCollection.findTypeface(textStyle);
    if (mustBeFound) {
      REPORTER_ASSERT(reporter, found != nullptr);

      SkString foundName;
      found->getFamilyName(&foundName);
      REPORTER_ASSERT(reporter, strcmp(foundName.c_str(), familyName));
      REPORTER_ASSERT(reporter, found->fontStyle() == fontStyle);
    } else {
      REPORTER_ASSERT(reporter, found == nullptr);
    }
  };
};

DEF_TEST(ParagraphBuilder, reporter) {
  ParagraphBuilderTester::TestParagraphBuilder(reporter);
}

DEF_TEST(ParagraphFontCollection, reporter) {
  ParagraphBuilderTester::TestFontCollection(reporter);
}