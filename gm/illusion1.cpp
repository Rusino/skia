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

#define W   800
#define H   600

#define LIGHT_GRAY 0xFFE4E4EC
#define RED 0xFFFB7D24
#define GRAY 0xFF94A4BB

#define CIRCLE_RAD 33
#define SMALL_RAD 15
#define BIG_RAD 55
#define BIG_DIST 125.0
#define BIG_X 200
#define BIG_Y 200
#define SMALL_DIST 55.0
#define SMALL_X 500
#define SMALL_Y 200
#define SHIFT 500

const SkScalar cos45 = SMALL_DIST / sqrt(2);
const SkScalar cos30 = BIG_DIST / 2;
const SkScalar cos60 = BIG_DIST * sqrt(3) / 2;

// Ebbinghaus Optical Illusion
class Illusion1GM : public skiagm::GM {
 public:
  Illusion1GM() {
    this->setBGColor(LIGHT_GRAY);
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion1");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint red;
    red.setColor(RED);
    red.setAntiAlias(true);

    SkPaint gray;
    gray.setColor(GRAY);
    gray.setAntiAlias(true);

    SkPaint light_gray;
    light_gray.setColor(LIGHT_GRAY);
    light_gray.setAntiAlias(true);

    canvas->save();

    gray.setDither(true);
    gray.setAntiAlias(true);
    gray.setTextSize(50);
    canvas->drawString("Ebbinghaus Optical Illusion", SMALL_X, BIG_Y + SHIFT / 2, gray);

    canvas->drawCircle(BIG_X - BIG_DIST, BIG_Y, BIG_RAD, gray);
    canvas->drawCircle(BIG_X - cos30, BIG_Y - cos60, BIG_RAD, gray);
    canvas->drawCircle(BIG_X + cos30, BIG_Y - cos60, BIG_RAD, gray);

    canvas->drawCircle(BIG_X, BIG_Y, CIRCLE_RAD, red);

    canvas->drawCircle(BIG_X + cos30, BIG_Y + cos60, BIG_RAD, gray);
    canvas->drawCircle(BIG_X - cos30, BIG_Y + cos60, BIG_RAD, gray);
    canvas->drawCircle(BIG_X + BIG_DIST, BIG_Y, BIG_RAD, gray);


    canvas->drawCircle(SMALL_X - SMALL_DIST, SMALL_Y, SMALL_RAD, gray);
    canvas->drawCircle(SMALL_X - cos45, SMALL_Y - cos45, SMALL_RAD, gray);
    canvas->drawCircle(SMALL_X, SMALL_Y - SMALL_DIST, SMALL_RAD, gray);
    canvas->drawCircle(SMALL_X + cos45, SMALL_Y - cos45, SMALL_RAD, gray);

    canvas->drawCircle(SMALL_X, SMALL_Y, CIRCLE_RAD, red);

    canvas->drawCircle(SMALL_X + SMALL_DIST, SMALL_Y, SMALL_RAD, gray);
    canvas->drawCircle(SMALL_X + cos45, SMALL_Y + cos45, SMALL_RAD, gray);
    canvas->drawCircle(SMALL_X, SMALL_Y + SMALL_DIST, SMALL_RAD, gray);
    canvas->drawCircle(SMALL_X - cos45, SMALL_Y + cos45, SMALL_RAD, gray);

    canvas->drawCircle(BIG_X, BIG_Y + SHIFT, BIG_DIST + BIG_RAD, gray);
    canvas->drawCircle(BIG_X, BIG_Y + SHIFT, BIG_DIST - BIG_RAD, light_gray);
    canvas->drawCircle(BIG_X, BIG_Y + SHIFT, CIRCLE_RAD, red);

    canvas->drawCircle(SMALL_X, SMALL_Y + SHIFT, SMALL_DIST + SMALL_RAD, gray);
    canvas->drawCircle(SMALL_X, SMALL_Y + SHIFT, SMALL_DIST - SMALL_RAD, light_gray);
    canvas->drawCircle(SMALL_X, SMALL_Y + SHIFT, CIRCLE_RAD, red);

    canvas->restore();
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion1GM; )

