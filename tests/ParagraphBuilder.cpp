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

#include "unicode/utypes.h"
#include "unicode/unistr.h"
#include "SkParagraphBuilder.h"
#include "SkParagraph.h"
#include "SkFontStyle.h"

#include "Test.h"
#include "sk_tool_utils.h"

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
        SkTextStyle ts2; ts1.setFontSize(20);
        ts2.setFontSize(20);
        ts2.setFontFamily("Arial");
        ts2.setBackgroundColor(SK_ColorBLUE);
        SkTextStyle ts3; ts1.setFontSize(30);
        ts3.setFontSize(30);
        ts3.setFontFamily("Arial");
        ts3.setBackgroundColor(SK_ColorLTGRAY);

        // Empty run set (will get default values
        std::vector<RunDef> input0 = {
            { paragraph, "", 10, ts.getFontFamily(), ts.getFontSize(), ts.getBackground().getColor()},
        };
        RunBuilderTest(reporter, input0, "", {});

        // Simple paragraph
        std::vector<RunDef> input1 = {
            { paragraph, "", 10, "", 0, SK_ColorTRANSPARENT},
            { push, "", 0, "Arial", 10, SK_ColorYELLOW},
            { add, "Simple paragraph.", 0, "", 0, SK_ColorTRANSPARENT},
        };
        std::vector<StyledText> output1 = {
            {0, 17, ts1},
        };
        RunBuilderTest(reporter, input1, "Simple paragraph.", output1);

        // Simple full coverage (list of level-1 styles)
      std::vector<RunDef> input2 = {
          { paragraph, "", 10, "", 0, SK_ColorTRANSPARENT},
          { push, "", 0,  "Arial", 10, SK_ColorYELLOW},
          { add, "Style #01 ", 0, "", 0, SK_ColorTRANSPARENT},
          { pop, "", 0, "", 0, SK_ColorTRANSPARENT},
          { push, "", 0,  "Arial", 20, SK_ColorBLUE},
          { add, "Style #02 ", 0, "", 0, SK_ColorTRANSPARENT},
          { pop, "", 0, "", 0, SK_ColorTRANSPARENT},
          { push, "", 0,  "Arial", 30, SK_ColorLTGRAY},
          { add, "Style #03 ", 0, "", 0, SK_ColorTRANSPARENT},
          { pop, "", 0, "", 0, SK_ColorTRANSPARENT},
      };

      std::vector<StyledText> output2 = {
          { 00, 10, ts1},
          { 10, 20, ts2},
          { 20, 30, ts3},
      };
      RunBuilderTest(reporter, input2,  "Style #01 Style #02 Style #03 ", output2);
    }

private:

    enum Command { add, push, pop, paragraph };
    struct TextStyle {
      const char* familyName;
      SkScalar fontSize;
      SkColor backgroundColor;
    };
    struct ParagraphStyle {
      size_t linesNumber;
      TextStyle textStyle;
    };


    struct RunDef {
      Command command;
      std::string text;
      size_t linesNumber;
      std::string familyName;
      SkScalar fontSize;
      SkColor backgroundColor;
    };

    static void RunBuilderTest(skiatest::Reporter* reporter,
                               const std::vector<RunDef>& commands,
                               const std::string& text,
                               std::vector<StyledText> runs
                               ) {
        if (!commands.empty()) {
            REPORTER_ASSERT(reporter, commands.size() > 0 && commands[0].command == paragraph);
        }

        SkParagraphStyle ps;
        ps.setMaxLines(commands[0].linesNumber);
        SkTextStyle ts;
        ts.setFontFamily(commands[0].familyName);
        ts.setFontSize(commands[0].fontSize);
        ts.setBackgroundColor(commands[0].backgroundColor);
        ps.setTextStyle(ts);

        SkParagraphBuilder builder(ps, std::make_shared<SkFontCollection>());
        for (size_t i = 1; i < commands.size(); ++i) {
            auto& command = commands[i];
            switch (command.command) {
              case add:
                  builder.AddText(command.text);
                  break;
              case push:
                  ts.setFontFamily(command.familyName);
                  ts.setFontSize(command.fontSize);
                  ts.setBackgroundColor(command.backgroundColor);
                  builder.PushStyle(ts);
                  break;
              case pop:
                  builder.Pop();
                  break;
              default:
                  REPORT_FAILURE(reporter, "Wrong command for SkParagraphBuilder.", SkString());
            }
        }

        // To imitate the "Build" call:
        builder.EndRunIfNeeded();

        if (builder._text.empty()) {
          REPORTER_ASSERT(reporter, text.empty());
        } else {
          icu::UnicodeString utf16 = icu::UnicodeString(&builder._text[0], builder._text.size());
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
};

DEF_TEST(ParagraphBuilder, reporter) {
  ParagraphBuilderTester::TestParagraphBuilder(reporter);
}