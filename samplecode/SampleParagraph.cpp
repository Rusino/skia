/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>
#include "SkParagraphBuilder.h"
#include "Sample.h"

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

static const std::vector<std::tuple<std::string, bool, bool, int, SkColor, SkColor, bool, SkTextDecorationStyle>> gParagraph = {
    { "monospace", true, false, 14, SK_ColorWHITE, SK_ColorRED, true, SkTextDecorationStyle::kDashed},
    { "Helvetica Neue", false, false, 20, SK_ColorWHITE, SK_ColorBLUE, false, SkTextDecorationStyle::kDotted},
    { "serif", true, true, 10, SK_ColorWHITE, SK_ColorRED, true, SkTextDecorationStyle::kDouble},
    { "Arial", false, true, 16, SK_ColorGRAY, SK_ColorWHITE, true, SkTextDecorationStyle::kSolid},
    { "sans-serif", false,  false, 8, SK_ColorWHITE, SK_ColorRED, false, SkTextDecorationStyle::kWavy}
};

class ParagraphView : public Sample {
public:
    ParagraphView() {
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
    }

protected:
    bool onQuery(Sample::Event* evt) override {
        if (Sample::TitleQ(*evt)) {
            Sample::TitleR(evt, "Paragraph");
            return true;
        }
        return this->INHERITED::onQuery(evt);
    }

    void drawTest(SkCanvas* canvas, SkScalar w, SkScalar h, SkColor fg, SkColor bg) {
        SkAutoCanvasRestore acr(canvas, true);

        canvas->clipRect(SkRect::MakeWH(w, h));
        canvas->drawColor(SK_ColorWHITE);

        SkScalar margin = 20;

        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setLCDRenderText(true);
        paint.setColor(fg);

        SkTextStyle style;
        style.setBackgroundColor(SK_ColorBLUE);
        style.setForegroundColor(paint);
        SkParagraphStyle paraStyle;
        paraStyle.setTextStyle(style);

        for (auto i = 1; i < 5; ++ i) {
          paraStyle.getTextStyle().setFontSize(24 * i);
          SkParagraphBuilder builder(paraStyle, nullptr);
          builder.AddText("Paragraph:");
          for (auto para : gParagraph) {
            SkTextStyle style;
            style.setBackgroundColor(bg);
            style.setForegroundColor(paint);
            style.setFontFamily(std::get<0>(para));
            SkFontStyle fontStyle(
                std::get<1>(para) ? SkFontStyle::Weight::kBold_Weight : SkFontStyle::Weight::kNormal_Weight,
                SkFontStyle::Width::kNormal_Width,
                std::get<2>(para) ? SkFontStyle::Slant::kItalic_Slant : SkFontStyle::Slant::kUpright_Slant);
            style.setFontStyle(fontStyle);
            style.setFontSize(std::get<3>(para) * i);
            style.setBackgroundColor(std::get<4>(para));
            SkPaint foreground;
            foreground.setColor(std::get<5>(para));
            style.setForegroundColor(foreground);
            if (std::get<6>(para)) {
              style.addShadow(SkTextShadow(SK_ColorBLACK, SkPoint::Make(5, 5), 2));
            }

            auto decoration = (i % 4);
            if (decoration == 3) { decoration = 4; }

            bool test = (SkTextDecoration)decoration != SkTextDecoration::kNone;
            if (test) {
              style.setDecoration((SkTextDecoration)decoration);
              style.setDecorationStyle(std::get<7>(para));
              style.setDecorationColor(std::get<5>(para));
              style.setDecorationThicknessMultiplier(4);
            }
            builder.PushStyle(style);
            std::string name = " " +
                std::get<0>(para) +
                (std::get<1>(para) ? ", bold" : "") +
                (std::get<2>(para) ? ", italic" : "") + " " +
                std::to_string(std::get<3>(para) * i) +
                (std::get<4>(para) != bg ? ", background" : "") +
                (std::get<5>(para) != fg ? ", foreground" : "") +
                (std::get<6>(para) ? ", shadow" : "") +
                (test ? ", decorations " : "") +
                ";";
            builder.AddText(name);
            builder.Pop();
          }

          auto paragraph = builder.Build();
          paragraph->Layout(w - margin);

          paragraph->Paint(canvas, margin, margin);

          canvas->translate(0, paragraph->GetHeight() + margin);
        }
    }

    void drawSimpleTest(SkCanvas* canvas, SkScalar w, SkScalar h,
                        SkColor fg = SK_ColorDKGRAY,
                        SkColor bg = SK_ColorWHITE,
                        std::string ff = "sans-serif",
                        SkScalar fs = 100,
                        bool shadow = false,
                        bool decoration = true
                        ) {
    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));
    canvas->drawColor(bg);

    SkScalar margin = 20;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setLCDRenderText(true);
    paint.setColor(fg);

    SkTextStyle style;
    style.setBackgroundColor(SK_ColorBLUE);
    style.setForegroundColor(paint);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);

    paraStyle.getTextStyle().setFontSize(10);
    SkParagraphBuilder builder(paraStyle, nullptr);

    style.setBackgroundColor(bg);
    style.setForegroundColor(paint);
    style.setFontFamily(ff);
    style.setFontStyle(SkFontStyle());
    style.setFontSize(fs);
    style.setBackgroundColor(bg);
    SkPaint foreground;
    foreground.setColor(fg);
    style.setForegroundColor(foreground);

    if (shadow) {
      style.addShadow(SkTextShadow(SK_ColorBLACK, SkPoint::Make(5, 5), 2));
    }

    if (decoration) {
      style.setDecoration(SkTextDecoration::kOverline);
      style.setDecorationStyle(SkTextDecorationStyle::kWavy);
      style.setDecorationColor(SK_ColorBLACK);
      style.setDecorationThicknessMultiplier(4);
    }
    builder.PushStyle(style);
    builder.AddText(gText);
    builder.Pop();

    auto paragraph = builder.Build();
    paragraph->Layout(w - margin);

    paragraph->Paint(canvas, margin, margin);

    canvas->translate(0, paragraph->GetHeight() + margin);
  }

    void drawText(SkCanvas* canvas, SkScalar w, SkScalar h,
                  const std::string& text,
                  SkColor fg = SK_ColorDKGRAY,
                  SkColor bg = SK_ColorWHITE,
                  std::string ff = "sans-serif",
                  SkScalar fs = 24) {
    SkAutoCanvasRestore acr(canvas, true);

    canvas->clipRect(SkRect::MakeWH(w, h));
    canvas->drawColor(bg);

    SkScalar margin = 20;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setLCDRenderText(true);
    paint.setColor(fg);

    SkTextStyle style;
    style.setBackgroundColor(SK_ColorBLUE);
    style.setForegroundColor(paint);
    SkParagraphStyle paraStyle;
    paraStyle.setTextStyle(style);

    paraStyle.getTextStyle().setFontSize(10);
    SkParagraphBuilder builder(paraStyle, std::make_shared<SkFontCollection>());

    style.setBackgroundColor(bg);
    style.setForegroundColor(paint);
    style.setFontFamily(ff);
    style.setFontStyle(SkFontStyle());
    style.setFontSize(fs);
    style.setBackgroundColor(bg);
    SkPaint foreground;
    foreground.setColor(fg);
    style.setForegroundColor(foreground);

    builder.PushStyle(style);
    builder.AddText(text);
    builder.Pop();

    auto paragraph = builder.Build();
    paragraph->Layout(w - margin);

    paragraph->Paint(canvas, margin, margin);

    canvas->translate(0, paragraph->GetHeight() + margin);
  }

    void onDrawContent(SkCanvas* canvas) override {
      //drawTest(canvas, this->width(), this->height(), SK_ColorRED, SK_ColorWHITE);
      //drawSimpleTest(canvas, this->width(), this->height());
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
      const std::string text = "My neighbor came over to say,\n"
                               "Although not in a neighborly way,\n\n"
                               "That he'd knock me around,\n\n\n"
                               "If I didn't stop the sound,\n\n\n\n"
                               "Of the classical music I play.";
      drawText(canvas, this->width(), this->height(), text, SK_ColorBLACK, SK_ColorWHITE);
    }

private:
    typedef Sample INHERITED;
};

//////////////////////////////////////////////////////////////////////////////

DEF_SAMPLE( return new ParagraphView(); )
