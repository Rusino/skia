/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkParagraphBuilder.h"
#include "Sample.h"


#include "Resources.h"
#include "SkBlurMaskFilter.h"
#include "SkCanvas.h"
#include "SkColorFilter.h"
#include "SkColorPriv.h"
#include "SkColorShader.h"
#include "SkGradientShader.h"
#include "SkGraphics.h"
#include "SkOSFile.h"
#include "SkPath.h"
#include "SkRandom.h"
#include "SkRegion.h"
#include "SkShader.h"
#include "SkParagraph.h"
#include "SkStream.h"
#include "SkTextBlob.h"
#include "SkTime.h"
#include "SkTypeface.h"
#include "SkUTF.h"

extern void skia_set_text_gamma(float blackGamma, float whiteGamma);

#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
extern SkTypeface* SkCreateTypefaceFromLOGFONT(const LOGFONT&);
#endif

static const char gShort[] = "Short text";
static const char gText[] =
    "When in the Course of human events it becomes necessary for one people "
    "to dissolve the political bands which have connected them with another "
    "and to assume among the powers of the earth, the separate and equal "
    "station to which the Laws of Nature and of Nature's God entitle them, "
    "a decent respect to the opinions of mankind requires that they should "
    "declare the causes which impel them to the separation.";

static const std::vector<std::tuple<std::string,
                                    bool,
                                    bool,
                                    int,
                                    SkColor,
                                    SkColor,
                                    bool,
                                    SkTextDecorationStyle>> gParagraph = {
    {"monospace", true, false, 14, SK_ColorWHITE, SK_ColorRED, true,
     SkTextDecorationStyle::kDashed},
    {"Assyrian", false, false, 20, SK_ColorWHITE, SK_ColorBLUE, false,
     SkTextDecorationStyle::kDotted},
    {"serif", true, true, 10, SK_ColorWHITE, SK_ColorRED, true,
     SkTextDecorationStyle::kDouble},
    {"Arial", false, true, 16, SK_ColorGRAY, SK_ColorGREEN, true,
     SkTextDecorationStyle::kSolid},
    {"sans-serif", false, false, 8, SK_ColorWHITE, SK_ColorRED, false,
     SkTextDecorationStyle::kWavy}
};

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

class ParagraphView1 : public Sample {
 public:
  ParagraphView1() {
#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
    LOGFONT lf;
        sk_bzero(&lf, sizeof(lf));
        lf.lfHeight = 9;
        SkTypeface* tf0 = SkCreateTypefaceFromLOGFONT(lf);
        lf.lfHeight = 12;
        SkTypeface* tf1 = SkCreateTypefaceFromLOGFONT(lf);
        // we assert that different sizes should not affect which face we get
        SkASSERT(tf0 == tf1);
        tf0->unref();
        tf1->unref();
#endif

    testFontProvider = sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
        "fonts/GoogleSans-Regular.ttf"));

    fontCollection = sk_make_sp<SkFontCollection>();
  }

  ~ParagraphView1() {
  }
 protected:
  bool onQuery(Sample::Event* evt) override {
    if (Sample::TitleQ(*evt)) {
      Sample::TitleR(evt, "Paragraph1");
      return true;
    }
    return this->INHERITED::onQuery(evt);
  }

  SkTextStyle style(SkPaint paint) {
    SkTextStyle style;
    paint.setAntiAlias(true);
    style.setForegroundColor(paint);
    style.setFontFamily("monospace");
    style.setFontSize(30);

    return style;
  }

  void drawTest(SkCanvas* canvas, SkScalar w, SkScalar h, SkColor fg, SkColor bg) {
    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));
    canvas->drawColor(SK_ColorWHITE);

    SkScalar margin = 20;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fg);

    SkPaint blue;
    blue.setColor(SK_ColorBLUE);

    SkPaint background;
    background.setColor(bg);

    SkTextStyle style;
    style.setBackgroundColor(blue);
    style.setForegroundColor(paint);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);

    for (auto i = 1; i < 5; ++i) {
      paraStyle.getTextStyle().setFontSize(24 * i);
      SkParagraphBuilder builder(paraStyle, sk_make_sp<SkFontCollection>());
      builder.addText("Paragraph:");
      for (auto para : gParagraph) {
        SkTextStyle style;
        style.setBackgroundColor(background);
        style.setForegroundColor(paint);
        style.setFontFamily(std::get<0>(para));
        SkFontStyle fontStyle(
            std::get<1>(para) ? SkFontStyle::Weight::kBold_Weight
                              : SkFontStyle::Weight::kNormal_Weight,
            SkFontStyle::Width::kNormal_Width,
            std::get<2>(para) ? SkFontStyle::Slant::kItalic_Slant
                              : SkFontStyle::Slant::kUpright_Slant);
        style.setFontStyle(fontStyle);
        style.setFontSize(std::get<3>(para) * i);
        SkPaint background;
        background.setColor(std::get<4>(para));
        style.setBackgroundColor(background);
        SkPaint foreground;
        foreground.setColor(std::get<5>(para));
        foreground.setAntiAlias(true);
        style.setForegroundColor(foreground);
        if (std::get<6>(para)) {
          style.addShadow(SkTextShadow(SK_ColorBLACK, SkPoint::Make(5, 5), 2));
        }

        auto decoration = (i % 4);
        if (decoration == 3) { decoration = 4; }

        bool test = (SkTextDecoration) decoration != SkTextDecoration::kNoDecoration;
        std::string deco = std::to_string((int) decoration);
        if (test) {
          style.setDecoration((SkTextDecoration) decoration);
          style.setDecorationStyle(std::get<7>(para));
          style.setDecorationColor(std::get<5>(para));
        }
        builder.pushStyle(style);
        std::string name = " " +
            std::get<0>(para) +
            (std::get<1>(para) ? ", bold" : "") +
            (std::get<2>(para) ? ", italic" : "") + " " +
            std::to_string(std::get<3>(para) * i) +
            (std::get<4>(para) != bg ? ", background" : "") +
            (std::get<5>(para) != fg ? ", foreground" : "") +
            (std::get<6>(para) ? ", shadow" : "") +
            (test ? ", decorations " + deco : "") +
            ";";
        builder.addText(name);
        builder.pop();
      }

      auto paragraph = builder.Build();
      paragraph->layout(w - margin * 2);

      paragraph->paint(canvas, margin, margin);

      canvas->translate(0, paragraph->getHeight());
    }
  }

  void drawSimpleTest(SkCanvas* canvas, SkScalar w, SkScalar h,
                      SkTextDecoration decoration,
                      SkTextDecorationStyle decorationStyle
  ) {

    SkColor fg = SK_ColorDKGRAY;
    SkColor bg = SK_ColorWHITE;
    std::string ff = "sans-serif";
    SkScalar fs = 20;
    bool shadow = false;
    bool has_decoration = true;

    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));
    canvas->drawColor(bg);

    SkScalar margin = 20;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fg);

    SkPaint background;
    background.setColor(bg);

    SkPaint blue;
    blue.setColor(SK_ColorBLUE);

    SkTextStyle style;
    style.setBackgroundColor(blue);
    style.setForegroundColor(paint);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);

    paraStyle.getTextStyle().setFontSize(10);
    SkParagraphBuilder builder(paraStyle, sk_make_sp<SkFontCollection>());

    style.setBackgroundColor(background);
    style.setForegroundColor(paint);
    style.setFontFamily(ff);
    style.setFontStyle(SkFontStyle());
    style.setFontSize(fs);
    style.setBackgroundColor(background);
    SkPaint foreground;
    foreground.setColor(fg);
    style.setForegroundColor(foreground);

    if (shadow) {
      style.addShadow(SkTextShadow(SK_ColorBLACK, SkPoint::Make(5, 5), 2));
    }

    if (has_decoration) {
      style.setDecoration(decoration);
      style.setDecorationStyle(decorationStyle);
      style.setDecorationColor(SK_ColorBLACK);
    }
    builder.pushStyle(style);
    builder.addText(gText);
    builder.pop();

    auto paragraph = builder.Build();
    paragraph->layout(w - margin);

    paragraph->paint(canvas, margin, margin);

    canvas->translate(0, paragraph->getHeight() + margin);
  }

  void onDrawContent(SkCanvas* canvas) override {
    drawTest(canvas, this->width(), this->height(), SK_ColorRED, SK_ColorWHITE);
    /*
    SkScalar height = this->height() / 5;
    drawSimpleTest(canvas, width(), height, SkTextDecoration::kOverline, SkTextDecorationStyle::kSolid);
    canvas->translate(0, height);
    drawSimpleTest(canvas, width(), height, SkTextDecoration::kUnderline, SkTextDecorationStyle::kWavy);
    canvas->translate(0, height);
    drawSimpleTest(canvas, width(), height, SkTextDecoration::kLineThrough, SkTextDecorationStyle::kWavy);
    canvas->translate(0, height);
    drawSimpleTest(canvas, width(), height, SkTextDecoration::kOverline, SkTextDecorationStyle::kDouble);
    canvas->translate(0, height);
    drawSimpleTest(canvas, width(), height, SkTextDecoration::kOverline, SkTextDecorationStyle::kWavy);
    };
    */
    /*
      SkScalar width = this->width() / 3;
      drawTest(canvas, width, this->height(), SK_ColorBLACK, SK_ColorWHITE);
      canvas->translate(width, 0);
      drawTest(canvas, width, this->height(), SK_ColorWHITE, SK_ColorBLACK);
      canvas->translate(width, 0);
      drawTest(canvas, width, this->height()/2, SK_ColorGRAY, SK_ColorWHITE);
      canvas->translate(0, this->height()/2);
      drawTest(canvas, width, this->height()/2, SK_ColorGRAY, SK_ColorBLACK);
    */
  }

 private:
  typedef Sample INHERITED;

  sk_sp<TestFontProvider> testFontProvider;
  sk_sp<SkFontCollection> fontCollection;
};

class ParagraphView2 : public Sample {
 public:
  ParagraphView2() {
#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
    LOGFONT lf;
        sk_bzero(&lf, sizeof(lf));
        lf.lfHeight = 9;
        SkTypeface* tf0 = SkCreateTypefaceFromLOGFONT(lf);
        lf.lfHeight = 12;
        SkTypeface* tf1 = SkCreateTypefaceFromLOGFONT(lf);
        // we assert that different sizes should not affect which face we get
        SkASSERT(tf0 == tf1);
        tf0->unref();
        tf1->unref();
#endif

    testFontProvider = sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
        "fonts/GoogleSans-Regular.ttf"));

    fontCollection = sk_make_sp<SkFontCollection>();
  }

  ~ParagraphView2() {
  }
 protected:
  bool onQuery(Sample::Event* evt) override {
    if (Sample::TitleQ(*evt)) {
      Sample::TitleR(evt, "Paragraph2");
      return true;
    }
    return this->INHERITED::onQuery(evt);
  }

  void drawCode(SkCanvas* canvas, SkScalar w, SkScalar h) {

    SkPaint comment;
    comment.setColor(SK_ColorGRAY);
    SkPaint constant;
    constant.setColor(SK_ColorMAGENTA);
    SkPaint null;
    null.setColor(SK_ColorMAGENTA);
    SkPaint literal;
    literal.setColor(SK_ColorGREEN);
    SkPaint code;
    code.setColor(SK_ColorDKGRAY);
    SkPaint number;
    number.setColor(SK_ColorBLUE);
    SkPaint name;
    name.setColor(SK_ColorRED);

    SkPaint white;
    white.setColor(SK_ColorWHITE);

    SkTextStyle defaultStyle;
    defaultStyle.setBackgroundColor(white);
    defaultStyle.setForegroundColor(code);
    defaultStyle.setFontFamily("monospace");
    defaultStyle.setFontSize(30);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(defaultStyle);

    SkParagraphBuilder builder(paraStyle, sk_make_sp<SkFontCollection>());

    //builder.PushStyle(style(comment));
    //builder.AddText("// Create a raised button.\n");
    //builder.Pop();

    builder.pushStyle(style(name));
    builder.addText("RaisedButton");
    builder.pop();
    builder.addText("(\n");
    builder.addText("  child: ");
    builder.pushStyle(style(constant));
    builder.addText("const");
    builder.pop();
    builder.addText(" ");
    builder.pushStyle(style(name));
    builder.addText("Text");
    builder.pop();
    builder.addText("(");
    builder.pushStyle(style(literal));
    builder.addText("'BUTTON TITLE'");
    builder.pop();
    builder.addText("),\n");

    auto paragraph = builder.Build();
    paragraph->layout(w - 20);

    paragraph->paint(canvas, 20, 20);
  }

  SkTextStyle style(SkPaint paint) {
    SkTextStyle style;
    paint.setAntiAlias(true);
    style.setForegroundColor(paint);
    style.setFontFamily("monospace");
    style.setFontSize(30);

    return style;
  }

  void drawText(SkCanvas* canvas, SkScalar w, SkScalar h,
                std::vector<std::string>& text,
                SkColor fg = SK_ColorDKGRAY,
                SkColor bg = SK_ColorWHITE,
                std::string ff = "sans-serif",
                SkScalar fs = 24,
                size_t lineLimit = std::numeric_limits<size_t>::max(),
                const std::u16string& ellipsis = u"\u2026") {

    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));
    canvas->drawColor(bg);

    SkScalar margin = 20;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(fg);

    SkPaint blue;
    blue.setColor(SK_ColorBLUE);

    SkPaint background;
    background.setColor(bg);

    SkTextStyle style;
    style.setBackgroundColor(blue);
    style.setForegroundColor(paint);
    style.setFontFamily(ff);
    style.setFontStyle(SkFontStyle(
        SkFontStyle::kMedium_Weight,
        SkFontStyle::kNormal_Width,
        SkFontStyle::kUpright_Slant ));
    style.setFontSize(fs);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);
    paraStyle.setMaxLines(lineLimit);

    paraStyle.setEllipsis(ellipsis);
    paraStyle.getTextStyle().setFontSize(20);
    fontCollection->setTestFontManager(testFontProvider);
    SkParagraphBuilder builder(paraStyle, fontCollection);

    SkPaint foreground;
    foreground.setColor(fg);
    style.setForegroundColor(foreground);
    style.setBackgroundColor(background);

    for (auto& part : text) {
      builder.pushStyle(style);
      builder.addText(part);
      builder.pop();
    }

    auto paragraph = builder.Build();
    paragraph->layout(w - margin * 2);
    paragraph->paint(canvas, margin, margin);

    canvas->translate(0, paragraph->getHeight() + margin);
  }

  void drawLine(SkCanvas* canvas, SkScalar w, SkScalar h,
                const std::string& text,
                SkTextAlign align) {
    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));
    canvas->drawColor(SK_ColorWHITE);

    SkScalar margin = 20;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorBLUE);

    SkPaint gray;
    gray.setColor(SK_ColorLTGRAY);

    SkTextStyle style;
    style.setBackgroundColor(gray);
    style.setForegroundColor(paint);
    style.setFontFamily("Arial");
    style.setFontSize(30);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);
    paraStyle.setTextAlign(align);

    SkParagraphBuilder builder(paraStyle, sk_make_sp<SkFontCollection>());
    builder.addText(text);

    auto paragraph = builder.Build();
    paragraph->layout(w - margin * 2);
    paragraph->layout(w - margin);
    paragraph->paint(canvas, margin, margin);

    canvas->translate(0, paragraph->getHeight() + margin);
  }

  void onDrawContent(SkCanvas* canvas) override {

    std::vector<std::string> code = {
        "// Create a flat button.\n",
        "FlatButton(\n",
        "  child: const Text('BUTTON TITLE'),\n",
        "  onPressed: () {\n",
        "    // Perform some action\n",
        "  }\n",
        ");"
        "\n\n",
        "// Create a disabled button.\n",
        "// Buttons are disabled when onPressed isn't\n",
        "// specified or is null.\n",
        "const FlatButton(\n  child: ",
        "Text('BUTTON TITLE'),\n",
        "  ",
        "onPressed: null\n",
        ");"
    };

    std::vector<std::string> cupertino = {
         "google_logogoogle_gsuper_g_logo 1 "
         "google_logogoogle_gsuper_g_logo 12 "
         "google_logogoogle_gsuper_g_logo 123 "
         "google_logogoogle_gsuper_g_logo 1234 "
         "google_logogoogle_gsuper_g_logo 12345 "
         "google_logogoogle_gsuper_g_logo 123456 "
         "google_logogoogle_gsuper_g_logo 1234567 "
         "google_logogoogle_gsuper_g_logo 12345678 "
         "google_logogoogle_gsuper_g_logo 123456789 "
         "google_logogoogle_gsuper_g_logo 1234567890 "
         "google_logogoogle_gsuper_g_logo 123456789 "
         "google_logogoogle_gsuper_g_logo 12345678 "
         "google_logogoogle_gsuper_g_logo 1234567 "
         "google_logogoogle_gsuper_g_logo 123456 "
         "google_logogoogle_gsuper_g_logo 12345 "
         "google_logogoogle_gsuper_g_logo 1234 "
         "google_logogoogle_gsuper_g_logo 123 "
         "google_logogoogle_gsuper_g_logo 12 "
         "google_logogoogle_gsuper_g_logo 1 "
         "google_logogoogle_gsuper_g_logo "
         "google_logogoogle_gsuper_g_logo "
         "google_logogoogle_gsuper_g_logo "
         "google_logogoogle_gsuper_g_logo "
         "google_logogoogle_gsuper_g_logo "
         "google_logogoogle_gsuper_g_logo"
    };
    std::vector<std::string> text = {
        "My neighbor came over to say,\n"
        "Although not in a neighborly way,\n\n"
        "That he'd knock me around,\n\n\n"
        "If I didn't stop the sound,\n\n\n\n"
        "Of the classical music I play."
    };

    std::string str(gText);
    std::vector<std::string> long_word =
        { "A_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_very_long_text"};

    std::vector<std::string> very_long =
        {"A very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very very long text"};

    SkScalar width = this->width() / 5;
    SkScalar height = this->height();
    drawText(canvas, width, height, long_word, SK_ColorBLACK, SK_ColorWHITE, "Google Sans", 30);
    canvas->translate(width, 0);
    drawText(canvas, width, height, very_long, SK_ColorBLACK, SK_ColorWHITE, "Google Sans", 30);
    canvas->translate(width, 0);
    drawText(canvas, width, height, text, SK_ColorBLACK, SK_ColorWHITE, "Roboto", 20, 100, u"\u2026");
    canvas->translate(width, 0);
    drawCode(canvas, width, height);
    canvas->translate(width, 0);
    drawText(canvas, width, height, cupertino, SK_ColorBLACK, SK_ColorWHITE, "Google Sans", 30);
  }

 private:
  typedef Sample INHERITED;

  sk_sp<TestFontProvider> testFontProvider;
  sk_sp<SkFontCollection> fontCollection;
};

class ParagraphView3 : public Sample {
 public:
  ParagraphView3() {
#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
    LOGFONT lf;
        sk_bzero(&lf, sizeof(lf));
        lf.lfHeight = 9;
        SkTypeface* tf0 = SkCreateTypefaceFromLOGFONT(lf);
        lf.lfHeight = 12;
        SkTypeface* tf1 = SkCreateTypefaceFromLOGFONT(lf);
        // we assert that different sizes should not affect which face we get
        SkASSERT(tf0 == tf1);
        tf0->unref();
        tf1->unref();
#endif

    testFontProvider = sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
        "fonts/GoogleSans-Regular.ttf"));

    fontCollection = sk_make_sp<SkFontCollection>();
  }

  ~ParagraphView3() {
  }
 protected:
  bool onQuery(Sample::Event* evt) override {
    if (Sample::TitleQ(*evt)) {
      Sample::TitleR(evt, "Paragraph3");
      return true;
    }
    return this->INHERITED::onQuery(evt);
  }

  SkTextStyle style(SkPaint paint) {
    SkTextStyle style;
    paint.setAntiAlias(true);
    style.setForegroundColor(paint);
    style.setFontFamily("monospace");
    style.setFontSize(30);

    return style;
  }

  void drawLine(SkCanvas* canvas, SkScalar w, SkScalar h,
                const std::string& text,
                SkTextAlign align,
                size_t lineLimit = std::numeric_limits<size_t>::max(),
                bool RTL = false,
                SkColor background = SK_ColorGRAY,
                const std::u16string& ellipsis = u"\u2026") {
    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));
    canvas->drawColor(SK_ColorWHITE);

    SkScalar margin = 20;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorBLACK);

    SkPaint gray;
    gray.setColor(background);

    SkPaint yellow;
    yellow.setColor(SK_ColorYELLOW);

    SkTextStyle style;
    style.setBackgroundColor(gray);
    style.setForegroundColor(paint);
    style.setFontFamily("sans-serif");
    style.setFontSize(30);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);
    paraStyle.setTextAlign(align);
    paraStyle.setMaxLines(lineLimit);
    paraStyle.setEllipsis(ellipsis);
    //paraStyle.setTextDirection(RTL ? SkTextDirection::rtl : SkTextDirection::ltr);

    SkParagraphBuilder builder(paraStyle, sk_make_sp<SkFontCollection>());
    if (RTL) {
      builder.addText(mirror(text));
    } else {
      builder.addText(normal(text));
    }

    canvas->drawRect(SkRect::MakeXYWH(margin, margin, w - margin * 2, h - margin * 2), yellow);
    auto paragraph = builder.Build();
    paragraph->layout(w - margin * 2);
    paragraph->paint(canvas, margin, margin);
  }

  std::u16string mirror(const std::string& text) {
    std::u16string result;
    result += u"\u202E";
    //for (auto i = text.size(); i > 0; --i) {
    //  result += text[i - 1];
    //}

    for (auto i = text.size(); i > 0; --i) {
      auto ch = text[i - 1];
      if (ch == ',') {
        result += u"!";
      } else if (ch == '.') {
          result += u"!";
      } else {
        result += ch;
      }
    }

    result += u"\u202C";
    return result;
  }

  std::u16string normal(const std::string& text) {
    std::u16string result;
    result += u"\u202D";
    for (auto i = 0ul; i < text.size(); ++i) {
      result += text[i];
    }
    result += u"\u202C";
    return result;
  }

  void onDrawContent(SkCanvas* canvas) override {

    const std::string options = // { "open-source open-source open-source open-source" };
                     {"Flutter is an open-source project to help developers "
                      "build high-performance, high-fidelity, mobile apps for "
                      "iOS and Android "
                      "from a single codebase. This design lab is a playground "
                      "and showcase of Flutter's many widgets, behaviors, "
                      "animations, layouts, and more."};

    canvas->drawColor(SK_ColorDKGRAY);
    SkScalar width = this->width() / 4;
    SkScalar height = this->height() / 2;

    const std::string line = "World domination is such an ugly phrase - I prefer to call it world optimisation";

    drawLine(canvas, width, height, line, SkTextAlign::left, 1, false, SK_ColorLTGRAY);
    canvas->translate(width, 0);
    drawLine(canvas, width, height, line, SkTextAlign::right, 2, false, SK_ColorLTGRAY);
    canvas->translate(width, 0);
    drawLine(canvas, width, height, line, SkTextAlign::center, 3, false, SK_ColorLTGRAY);
    canvas->translate(width, 0);
    drawLine(canvas, width, height, line, SkTextAlign::justify, 4, false, SK_ColorLTGRAY);
    canvas->translate(-width * 3, height);

    drawLine(canvas, width, height, line, SkTextAlign::left, 1, true, SK_ColorLTGRAY);
    canvas->translate(width, 0);
    drawLine(canvas, width, height, line, SkTextAlign::right, 2, true, SK_ColorLTGRAY);
    canvas->translate(width, 0);
    drawLine(canvas, width, height, line, SkTextAlign::center, 3, true, SK_ColorLTGRAY);
    canvas->translate(width, 0);
    drawLine(canvas, width, height, line, SkTextAlign::justify, 4, true, SK_ColorLTGRAY);
    canvas->translate(width, 0);
  }

 private:
  typedef Sample INHERITED;

  sk_sp<TestFontProvider> testFontProvider;
  sk_sp<SkFontCollection> fontCollection;
};

class ParagraphView4 : public Sample {
 public:
  ParagraphView4() {
#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
    LOGFONT lf;
        sk_bzero(&lf, sizeof(lf));
        lf.lfHeight = 9;
        SkTypeface* tf0 = SkCreateTypefaceFromLOGFONT(lf);
        lf.lfHeight = 12;
        SkTypeface* tf1 = SkCreateTypefaceFromLOGFONT(lf);
        // we assert that different sizes should not affect which face we get
        SkASSERT(tf0 == tf1);
        tf0->unref();
        tf1->unref();
#endif

    testFontProvider = sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
        "fonts/GoogleSans-Regular.ttf"));

    fontCollection = sk_make_sp<SkFontCollection>();
  }

  ~ParagraphView4() {
  }
 protected:
  bool onQuery(Sample::Event* evt) override {
    if (Sample::TitleQ(*evt)) {
      Sample::TitleR(evt, "Paragraph4");
      return true;
    }
    return this->INHERITED::onQuery(evt);
  }

  SkTextStyle style(SkPaint paint) {
    SkTextStyle style;
    paint.setAntiAlias(true);
    style.setForegroundColor(paint);
    style.setFontFamily("monospace");
    style.setFontSize(30);

    return style;
  }

  void drawFlutter(SkCanvas* canvas, SkScalar w, SkScalar h,
                std::string ff = "Google Sans",
                SkScalar fs = 30,
                size_t lineLimit = std::numeric_limits<size_t>::max(),
                const std::u16string& ellipsis = u"\u2026") {

    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));

    SkScalar margin = 20;

    SkPaint black;
    black.setAntiAlias(true);
    black.setColor(SK_ColorBLACK);

    SkPaint blue;
    blue.setAntiAlias(true);
    blue.setColor(SK_ColorBLUE);

    SkPaint red;
    red.setAntiAlias(true);
    red.setColor(SK_ColorRED);

    SkPaint green;
    green.setAntiAlias(true);
    green.setColor(SK_ColorGREEN);

    SkPaint gray;
    gray.setColor(SK_ColorLTGRAY);

    SkPaint yellow;
    yellow.setColor(SK_ColorYELLOW);

    SkPaint magenta;
    magenta.setAntiAlias(true);
    magenta.setColor(SK_ColorMAGENTA);

    SkTextStyle style;
    style.setFontFamily(ff);
    style.setFontSize(fs);

    SkTextStyle style0;
    style0.setForegroundColor(black);
    style0.setBackgroundColor(gray);
    style0.setFontFamily(ff);
    style0.setFontSize(fs);
    style0.setDecoration(SkTextDecoration::kUnderline);
    style0.setDecorationStyle(SkTextDecorationStyle::kDouble);
    style0.setDecorationColor(SK_ColorBLACK);

    SkTextStyle style1;
    style1.setForegroundColor(blue);
    style1.setBackgroundColor(yellow);
    style1.setFontFamily(ff);
    style1.setFontSize(fs);
    style1.setDecoration(SkTextDecoration::kOverline);
    style1.setDecorationStyle(SkTextDecorationStyle::kWavy);
    style1.setDecorationColor(SK_ColorBLACK);

    SkTextStyle style2;
    style2.setForegroundColor(red);
    style2.setFontFamily(ff);
    style2.setFontSize(fs);

    SkTextStyle style3;
    style3.setForegroundColor(green);
    style3.setFontFamily(ff);
    style3.setFontSize(fs);

    SkTextStyle style4;
    style4.setForegroundColor(magenta);
    style4.setFontFamily(ff);
    style4.setFontSize(fs);

    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);
    paraStyle.setMaxLines(lineLimit);

    paraStyle.setEllipsis(ellipsis);
    fontCollection->setTestFontManager(testFontProvider);

    const std::string logo1 = "google_";
    const std::string logo2 = "logo";
    const std::string logo3 = "go";
    const std::string logo4 = "ogle_logo";
    const std::string logo5 = "google_lo";
    const std::string logo6 = "go";
    {
      SkParagraphBuilder builder(paraStyle, fontCollection);

      builder.pushStyle(style0);
      builder.addText(logo1);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo2);
      builder.pop();

      builder.addText(" ");

      builder.pushStyle(style0);
      builder.addText(logo3);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo4);
      builder.pop();

      builder.addText(" ");

      builder.pushStyle(style0);
      builder.addText(logo5);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo6);
      builder.pop();

      auto paragraph = builder.Build();
      paragraph->layout(w - margin * 2);
      paragraph->paint(canvas, margin, margin);
      canvas->translate(0, h + margin);
    }
  }

  void onDrawContent(SkCanvas* canvas) override {

    canvas->drawColor(SK_ColorWHITE);
    SkScalar width = this->width();
    SkScalar height = this->height();

    drawFlutter(canvas, width, height/2);
  }

 private:
  typedef Sample INHERITED;

  sk_sp<TestFontProvider> testFontProvider;
  sk_sp<SkFontCollection> fontCollection;
};

class ParagraphView5 : public Sample {
 public:
  ParagraphView5() {
#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
    LOGFONT lf;
        sk_bzero(&lf, sizeof(lf));
        lf.lfHeight = 9;
        SkTypeface* tf0 = SkCreateTypefaceFromLOGFONT(lf);
        lf.lfHeight = 12;
        SkTypeface* tf1 = SkCreateTypefaceFromLOGFONT(lf);
        // we assert that different sizes should not affect which face we get
        SkASSERT(tf0 == tf1);
        tf0->unref();
        tf1->unref();
#endif

    testFontProvider = sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
        "fonts/GoogleSans-Regular.ttf"));

    fontCollection = sk_make_sp<SkFontCollection>();
  }

  ~ParagraphView5() {
  }
 protected:
  bool onQuery(Sample::Event* evt) override {
    if (Sample::TitleQ(*evt)) {
      Sample::TitleR(evt, "Paragraph4");
      return true;
    }
    return this->INHERITED::onQuery(evt);
  }

  SkTextStyle style(SkPaint paint) {
    SkTextStyle style;
    paint.setAntiAlias(true);
    style.setForegroundColor(paint);
    style.setFontFamily("monospace");
    style.setFontSize(30);

    return style;
  }

  void bidi(SkCanvas* canvas, SkScalar w, SkScalar h,
             std::u16string text,
             std::u16string expected,
             size_t lineLimit = std::numeric_limits<size_t>::max(),
             std::string ff = "sans-serif",
             SkScalar fs = 30,
             const std::u16string& ellipsis = u"\u2026") {

    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));

    SkScalar margin = 20;

    SkPaint black;
    black.setColor(SK_ColorBLACK);
    SkPaint gray;
    gray.setColor(SK_ColorLTGRAY);

    SkTextStyle style;
    style.setForegroundColor(black);
    style.setFontFamily(ff);
    style.setFontSize(fs);

    SkTextStyle style0;
    style0.setForegroundColor(black);
    style0.setFontFamily(ff);
    style0.setFontSize(fs);
    style0.setFontStyle(SkFontStyle(SkFontStyle::kNormal_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kItalic_Slant));

    SkTextStyle style1;
    style1.setForegroundColor(gray);
    style1.setFontFamily(ff);
    style1.setFontSize(fs);
    style1.setFontStyle(SkFontStyle(SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));

    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);
    paraStyle.setMaxLines(lineLimit);

    paraStyle.setEllipsis(ellipsis);
    fontCollection->setTestFontManager(testFontProvider);

    SkParagraphBuilder builder(paraStyle, fontCollection);

    if (text.empty()) {
      const std::u16string text0 = u"\u202Dabc";
      const std::u16string text1 = u"\u202EFED";
      const std::u16string text2 = u"\u202Dghi";
      const std::u16string text3 = u"\u202ELKJ";
      const std::u16string text4 = u"\u202Dmno";
      builder.pushStyle(style0);
      builder.addText(text0);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(text1);
      builder.pop();
      builder.pushStyle(style0);
      builder.addText(text2);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(text3);
      builder.pop();
      builder.pushStyle(style0);
      builder.addText(text4);
      builder.pop();
    } else {
      icu::UnicodeString unicode((UChar*) text.data(), SkToS32(text.size()));
      std::string str;
      unicode.toUTF8String(str);
      SkDebugf("Text: %s\n", str.c_str());
      builder.addText(text + expected);
    }

    auto paragraph = builder.Build();
    paragraph->layout(w - margin * 2);
    paragraph->paint(canvas, margin, margin);
  }

  void onDrawContent(SkCanvas* canvas) override {

    canvas->drawColor(SK_ColorWHITE);
    SkScalar width = this->width();
    SkScalar height = this->height() / 3;

    const std::u16string text1 = u"A \u202ENAC\u202Cner, exceedingly \u202ENAC\u202Cny,\n"
                                 "One morning remarked to his granny:\n"
                                 "A \u202ENAC\u202Cner \u202ENAC\u202C \u202ENAC\u202C,\n"
                                 "Anything that he \u202ENAC\u202C,\n"
                                 "But a \u202ENAC\u202Cner \u202ENAC\u202C't \u202ENAC\u202C a \u202ENAC\u202C, \u202ENAC\u202C he?";
    //bidi(canvas, width, height, text1, u"", 5);
    //canvas->translate(0, height);

    //bidi(canvas, width, height, u"\u2067DETALOSI\u2069", u"");
    //canvas->translate(0, height);

    //bidi(canvas, width, height, u"\u202BDEDDEBME\u202C", u"");
    //canvas->translate(0, height);

    //bidi(canvas, width, height, u"\u202EEDIRREVO\u202C", u"");
    //canvas->translate(0, height);

    //bidi(canvas, width, height, u"\u200FTICILPMI\u200E", u"");
    //canvas->translate(0, height);

    bidi(canvas, width, height, u"123 456 7890 \u202EZYXWV UTS RQP ONM LKJ IHG FED CBA\u202C.", u"", 2);
    canvas->translate(0, height);

    //bidi(canvas, width, height, u"", u"");
    //canvas->translate(0, height);

    return;
  }

 private:
  typedef Sample INHERITED;

  sk_sp<TestFontProvider> testFontProvider;
  sk_sp<SkFontCollection> fontCollection;
};

class ParagraphView6 : public Sample {
 public:
  ParagraphView6() {
#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
    LOGFONT lf;
        sk_bzero(&lf, sizeof(lf));
        lf.lfHeight = 9;
        SkTypeface* tf0 = SkCreateTypefaceFromLOGFONT(lf);
        lf.lfHeight = 12;
        SkTypeface* tf1 = SkCreateTypefaceFromLOGFONT(lf);
        // we assert that different sizes should not affect which face we get
        SkASSERT(tf0 == tf1);
        tf0->unref();
        tf1->unref();
#endif

    testFontProvider = sk_make_sp<TestFontProvider>(MakeResourceAsTypeface(
        "fonts/HangingS.ttf"));

    fontCollection = sk_make_sp<SkFontCollection>();
  }

  ~ParagraphView6() {
  }
 protected:
  bool onQuery(Sample::Event* evt) override {
    if (Sample::TitleQ(*evt)) {
      Sample::TitleR(evt, "Paragraph4");
      return true;
    }
    return this->INHERITED::onQuery(evt);
  }

  SkTextStyle style(SkPaint paint) {
    SkTextStyle style;
    paint.setAntiAlias(true);
    style.setForegroundColor(paint);
    style.setFontFamily("monospace");
    style.setFontSize(30);

    return style;
  }

  void hangingS(SkCanvas* canvas, SkScalar w, SkScalar h, SkScalar fs = 60.0) {

    auto ff = "HangingS";

    canvas->drawColor(SK_ColorLTGRAY);

    SkPaint black;
    black.setAntiAlias(true);
    black.setColor(SK_ColorBLACK);

    SkPaint blue;
    blue.setAntiAlias(true);
    blue.setColor(SK_ColorBLUE);

    SkPaint red;
    red.setAntiAlias(true);
    red.setColor(SK_ColorRED);

    SkPaint green;
    green.setAntiAlias(true);
    green.setColor(SK_ColorGREEN);

    SkPaint gray;
    gray.setColor(SK_ColorCYAN);

    SkPaint yellow;
    yellow.setColor(SK_ColorYELLOW);

    SkPaint magenta;
    magenta.setAntiAlias(true);
    magenta.setColor(SK_ColorMAGENTA);

    SkFontStyle fontStyle(SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kItalic_Slant);

    SkTextStyle style;
    style.setFontFamily(ff);
    style.setFontSize(fs);
    style.setFontStyle(fontStyle);

    SkTextStyle style0;
    style0.setForegroundColor(black);
    style0.setBackgroundColor(gray);
    style0.setFontFamily(ff);
    style0.setFontSize(fs);
    style0.setFontStyle(fontStyle);

    SkTextStyle style1;
    style1.setForegroundColor(blue);
    style1.setBackgroundColor(yellow);
    style1.setFontFamily(ff);
    style1.setFontSize(fs);
    style1.setFontStyle(fontStyle);

    SkTextStyle style2;
    style2.setForegroundColor(red);
    style2.setFontFamily(ff);
    style2.setFontSize(fs);
    style2.setFontStyle(fontStyle);

    SkTextStyle style3;
    style3.setForegroundColor(green);
    style3.setFontFamily(ff);
    style3.setFontSize(fs);
    style3.setFontStyle(fontStyle);

    SkTextStyle style4;
    style4.setForegroundColor(magenta);
    style4.setFontFamily(ff);
    style4.setFontSize(fs);
    style4.setFontStyle(fontStyle);

    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);

    fontCollection->setTestFontManager(testFontProvider);

    const std::string logo1 = "S";
    const std::string logo2 = "kia";
    const std::string logo3 = "Sk";
    const std::string logo4 = "ia";
    const std::string logo5 = "Ski";
    const std::string logo6 = "a";
    {
      SkParagraphBuilder builder(paraStyle, fontCollection);

      builder.pushStyle(style0);
      builder.addText(logo1);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo2);
      builder.pop();

      builder.addText("   ");

      builder.pushStyle(style0);
      builder.addText(logo3);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo4);
      builder.pop();

      builder.addText("   ");

      builder.pushStyle(style0);
      builder.addText(logo5);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo6);
      builder.pop();

      auto paragraph = builder.Build();
      paragraph->layout(w);
      paragraph->paint(canvas, 40, 40);
      canvas->translate(0, h);
    }

    const std::string logo11 = "S";
    const std::string logo12 = "S";
    const std::string logo13 = "S";
    const std::string logo14 = "S";
    const std::string logo15 = "S";
    const std::string logo16 = "S";
    {
      SkParagraphBuilder builder(paraStyle, fontCollection);

      builder.pushStyle(style0);
      builder.addText(logo11);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo12);
      builder.pop();

      builder.addText("   ");

      builder.pushStyle(style0);
      builder.addText(logo13);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo14);
      builder.pop();

      builder.addText("   ");

      builder.pushStyle(style0);
      builder.addText(logo15);
      builder.pop();
      builder.pushStyle(style1);
      builder.addText(logo16);
      builder.pop();

      auto paragraph = builder.Build();
      paragraph->layout(w);
      paragraph->paint(canvas, 40, h);
      canvas->translate(0, h);
    }
  }

  void onDrawContent(SkCanvas* canvas) override {

    canvas->drawColor(SK_ColorWHITE);
    SkScalar width = this->width();
    SkScalar height = this->height()/4;

    hangingS(canvas, width, height);

    return;
  }

 private:
  typedef Sample INHERITED;

  sk_sp<TestFontProvider> testFontProvider;
  sk_sp<SkFontCollection> fontCollection;
};

class ParagraphView7 : public Sample {
 public:
  ParagraphView7() {
#if defined(SK_BUILD_FOR_WIN) && defined(SK_FONTHOST_WIN_GDI)
    LOGFONT lf;
        sk_bzero(&lf, sizeof(lf));
        lf.lfHeight = 9;
        SkTypeface* tf0 = SkCreateTypefaceFromLOGFONT(lf);
        lf.lfHeight = 12;
        SkTypeface* tf1 = SkCreateTypefaceFromLOGFONT(lf);
        // we assert that different sizes should not affect which face we get
        SkASSERT(tf0 == tf1);
        tf0->unref();
        tf1->unref();
#endif

    fontCollection = sk_make_sp<SkFontCollection>();
  }

  ~ParagraphView7() {
  }
 protected:
  bool onQuery(Sample::Event* evt) override {
    if (Sample::TitleQ(*evt)) {
      Sample::TitleR(evt, "Paragraph7");
      return true;
    }
    return this->INHERITED::onQuery(evt);
  }

  void onDrawContent(SkCanvas* canvas) override {

    //SetResourcePath
    //("/usr/local/google/home/jlavrova/Sources/flutter/engine/src/flutter/third_party/txt/third_party/fonts");
    testFontProvider = sk_make_sp<TestFontProvider>(MakeResourceAsTypeface("fonts/Roboto-Regular.ttf"));
    fontCollection->setTestFontManager(testFontProvider);

    SkParagraphStyle paragraphStyle;
    paragraphStyle.setTextAlign(SkTextAlign::left);
    paragraphStyle.setMaxLines(10);
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

    canvas->drawColor(SK_ColorWHITE);
    canvas->translate(20, 20);

    RectHeightStyle heightStyle = RectHeightStyle::kMax;
    RectWidthStyle widthStyle = RectWidthStyle::kTight;
    {
      auto result = paragraph->getRectsForRange(0, 0, heightStyle, widthStyle);
      SkASSERT(result.size() == 0);
    }

    {
      auto result = paragraph->getRectsForRange(0, 1, heightStyle, widthStyle);
      SkASSERT(result.size() == 1);
      SkPaint red; red.setColor(SK_ColorRED);
      canvas->drawRect(result[0].rect, red);
      //SkASSERT(SkScalarNearlyEqual(result[0].rect.left(), 0));
      //SkASSERT(SkScalarNearlyEqual(result[0].rect.top(), 0.40625));
      //SkASSERT(SkScalarNearlyEqual(result[0].rect.right(), 28.417969));
      //SkASSERT(SkScalarNearlyEqual(result[0].rect.bottom(), 59));
    }
/*
    {
      auto result = paragraph->getRectsForRange(2, 8, heightStyle, widthStyle);
      SkASSERT(result.size() == 1);
      SkASSERT(SkScalarNearlyEqual(result[0].rect.left(), 56.835938));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.top(), 0.40625));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.right(), 177.97266));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.bottom(), 59));
    }

    {
      auto result = paragraph->getRectsForRange(8, 21, heightStyle, widthStyle);
      SkASSERT(result.size() == 1);
      SkASSERT(SkScalarNearlyEqual(result[0].rect.left(), 177.97266));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.top(), 0.40625));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.right(), 507.02344));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.bottom(), 59));
    }

    {
      auto result = paragraph->getRectsForRange(8, 21, heightStyle, widthStyle);
      SkASSERT(result.size() == 4);
      SkASSERT(SkScalarNearlyEqual(result[0].rect.left(), 211.375));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.top(), 59.40625));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.right(), 463.61719));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.bottom(), 118));
      // TODO(garyq): The following set of vals are definetly wrong and
      // end of paragraph handling needs to be fixed in a later patch.
      SkASSERT(SkScalarNearlyEqual(result[3].rect.left(), 0));
      SkASSERT(SkScalarNearlyEqual(result[3].rect.top(), 236.40625));
      SkASSERT(SkScalarNearlyEqual(result[3].rect.right(), 142.08984));
      SkASSERT(SkScalarNearlyEqual(result[3].rect.bottom(), 295));
    }

    {
      auto result = paragraph->getRectsForRange(19, 22, heightStyle, widthStyle);
      SkASSERT(result.size() == 1);
      SkASSERT(SkScalarNearlyEqual(result[0].rect.left(), 450.1875));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.top(), 0.40625));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.right(), 519.47266));
      SkASSERT(SkScalarNearlyEqual(result[0].rect.bottom(), 59));
    }

    {
      auto result = paragraph->getRectsForRange(21, 21, heightStyle, widthStyle);
      SkASSERT(result.size() == 0);
    }
*/
    paragraph->paint(canvas, 0, 0);
    return;
  }

 private:
  typedef Sample INHERITED;

  sk_sp<TestFontProvider> testFontProvider;
  sk_sp<SkFontCollection> fontCollection;
};

/*
 *                       ui.TextStyle textStyle = ui.TextStyle(
                        fontFamily: "Roboto",
                        fontSize: 50,
                        fontWeight: FontWeight.w500,
                        color: Colors.black,
                        height: 1
                      );

                      ui.ParagraphBuilder builder = ui.ParagraphBuilder(
                          ui.ParagraphStyle(textAlign: TextAlign.left, maxLines: 10));
                      builder.pushStyle(textStyle);
                      builder.addText('12345,  "67890" 12345 67890 12345 67890 12345 67890 12345 67890 12345 67890 12345');
                      builder.pop();

                      var paragraph = builder.build();
                      paragraph.layout(ui.ParagraphConstraints(width: 550));

                      var result1 =
                        paragraph.getBoxesForRange(0, 1,
                            boxHeightStyle: ui.BoxHeightStyle.max, boxWidthStyle: ui.BoxWidthStyle.tight);
 */
//////////////////////////////////////////////////////////////////////////////

DEF_SAMPLE(return new ParagraphView1();)
DEF_SAMPLE(return new ParagraphView2();)
DEF_SAMPLE(return new ParagraphView3();)
DEF_SAMPLE(return new ParagraphView4();)
DEF_SAMPLE(return new ParagraphView5();)
DEF_SAMPLE(return new ParagraphView6();)
DEF_SAMPLE(return new ParagraphView7();)
