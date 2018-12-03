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

#define W   800
#define H   600

#define RED SK_ColorRED
#define BLACK SK_ColorBLACK

#define HH 600.0
#define WW 200.0
#define START 100
#define WIDTH 8

// Same Length Red Lines?
class Illusion7GM : public skiagm::GM {
 public:
  Illusion7GM() {
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion7");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint red;
    red.setColor(RED);
    red.setAntiAlias(true);
    red.setStrokeWidth(WIDTH);

    SkPaint black;
    black.setColor(BLACK);
    black.setAntiAlias(true);

    red.setDither(true);
    red.setAntiAlias(true);
    red.setTextSize(50);
    canvas->drawString("Same Length Red Lines?", 50, 50, red);

    SkPaint paint;
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(WIDTH);

    const SkScalar HH3 = HH / 3;
    const SkScalar DD = WW + WW * 3 / 2;
    const SkScalar hh3 = HH3 / 3;
    const SkScalar ww = WW * 2 / 3;

    SkPath front;
    front.moveTo(START, START);
    front.lineTo(START, START + HH);
    front.lineTo(START + WW, START + HH);
    front.lineTo(START + WW, START);
    front.lineTo(START, START);

    front.moveTo(START, START + HH3);
    front.lineTo(START + WW, START + HH3);

    front.moveTo(START, START + HH3 * 2);
    front.lineTo(START + WW, START + HH3 * 2);

    front.close();
    canvas->drawPath(front, paint);

    canvas->drawLine(START + WW, START + HH3, START + WW, START + HH3 * 2, red);

    SkPath side;
    side.moveTo(START + WW, START);
    side.lineTo(START + DD, START + HH3);

    side.moveTo(START + WW, START + HH3);
    side.lineTo(START + DD, START + HH3 + hh3);

    side.moveTo(START + WW, START + HH3 * 2);
    side.lineTo(START + DD, START + HH3 + hh3 * 2);

    side.moveTo(START + WW, START + HH);
    side.lineTo(START + DD, START + HH3 + HH3);

    side.close();
    canvas->drawPath(side, paint);

    canvas->drawLine(START + DD, START + HH3, START + DD, START + HH3 * 2, red);

    SkPath back;
    back.moveTo(START + DD, START + HH3);
    back.lineTo(START + DD + ww, START + HH3);
    back.lineTo(START + DD + ww, START + HH3 + HH3);
    back.lineTo(START + DD, START + HH3 + HH3);

    back.moveTo(START + DD, START + HH3 + hh3);
    back.lineTo(START + DD + ww, START + HH3 + hh3);

    back.moveTo(START + DD, START + HH3 + hh3 * 2);
    back.lineTo(START + DD + ww, START + HH3 + hh3 * 2);

    back.close();
    canvas->drawPath(back, paint);
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion7GM; )

