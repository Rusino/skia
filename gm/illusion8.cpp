/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gm.h"
#include "sk_tool_utils.h"
#include "SkCanvas.h"
#include "SkPath.h"
#include "SkAnimTimer.h"
#include "SkGradientShader.h"

#define W   800
#define H   600

#define DKGRAY SK_ColorBLACK
#define GRAY 0xff848484
#define LTGRAY SK_ColorWHITE

#define HBIG 300.0
#define WBIG 600.0
#define HSMALL 100.0
#define WSMALL 200.0
#define START 100

const SkScalar HDIFF = (HBIG - HSMALL) / 2;
const SkScalar WDIFF = (WBIG - WSMALL) / 2;

// Gradient Illusion
class Illusion8GM : public skiagm::GM {
 public:
  Illusion8GM() {
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion8");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint light_gray;
    light_gray.setColor(LTGRAY);
    light_gray.setAntiAlias(true);

    SkPaint dark_gray;
    dark_gray.setColor(DKGRAY);
    dark_gray.setAntiAlias(true);

    dark_gray.setDither(true);
    dark_gray.setAntiAlias(true);
    dark_gray.setTextSize(50);
    canvas->drawString("Gradient Illusion", 50, 50, dark_gray);

    {
      SkRect big = SkRect::MakeXYWH(START, START, WBIG, HBIG);
      SkColor colors[] = {DKGRAY, LTGRAY};
      SkPoint points[] = {{big.fLeft, big.fTop}, {big.fRight, big.fTop}};
      SkPaint paint;
      paint.setShader(SkGradientShader::MakeLinear(points,
                                                   colors,
                                                   nullptr,
                                                   2,
                                                   SkShader::kClamp_TileMode,
                                                   0,
                                                   nullptr));
      canvas->drawRect(big, paint);

      paint.setShader(nullptr);
      paint.setStyle(SkPaint::kStroke_Style);
      canvas->drawRect(big, paint);
    }
    {
      SkRect small = SkRect::MakeXYWH(START + WDIFF, START + HDIFF, WSMALL, HSMALL);
      SkPaint paint;
      paint.setColor(GRAY);
      canvas->drawRect(small,paint );
    }
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion8GM; )

