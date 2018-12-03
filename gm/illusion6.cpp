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

#define GRAY SK_ColorGRAY
#define BLACK SK_ColorBLACK

#define CUBE 100.0
#define START 100

const SkScalar center = CUBE + CUBE / 2;

// Shrinking Square?
class Illusion6GM : public skiagm::GM {
 public:
  Illusion6GM() : fAngle(0.0) {
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion6");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint gray;
    gray.setColor(GRAY);
    gray.setAntiAlias(true);

    SkPaint black;
    black.setColor(BLACK);
    black.setAntiAlias(true);

    gray.setDither(true);
    gray.setAntiAlias(true);
    gray.setTextSize(50);
    canvas->drawString("Shrinking Square?", 50, 50, gray);

    canvas->save();

    canvas->translate(center + START, center + START);
    canvas->rotate(fAngle);

    SkRect bar = SkRect::MakeXYWH(- CUBE, - CUBE, CUBE * 2, CUBE * 2);
    canvas->drawRect(bar, gray);
    canvas->restore();

    canvas->save();

    SkRect top_left = SkRect::MakeXYWH(START, START, CUBE, CUBE);
    canvas->drawRect(top_left, black);

    SkRect top_right = SkRect::MakeXYWH(START + CUBE * 2, START, CUBE, CUBE);
    canvas->drawRect(top_right, black);

    SkRect bottom_left = SkRect::MakeXYWH(START, START + CUBE * 2, CUBE, CUBE);
    canvas->drawRect(bottom_left, black);

    SkRect bottom_right = SkRect::MakeXYWH(START + CUBE * 2, START + CUBE * 2, CUBE, CUBE);
    canvas->drawRect(bottom_right, black);

    canvas->restore();
  }

  bool onAnimate(const SkAnimTimer& timer) override {

    fAngle = timer.secs() * 50;
    return true;
  }

 private:
  typedef skiagm::GM INHERITED;

  SkScalar fAngle;
};

DEF_GM( return new Illusion6GM; )

