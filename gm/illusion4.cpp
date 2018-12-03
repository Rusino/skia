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

#define RED SK_ColorRED
#define BLACK SK_ColorBLACK

#define SIZE 400.0
#define WIDTH 8.0
#define START 100
#define COUNT 40
#define BARS 3
#define BAR 15

// Hering Illusion
class Illusion4GM : public skiagm::GM {
 public:
  Illusion4GM() {
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion4");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint red;
    red.setColor(RED);
    red.setAntiAlias(true);

    SkPaint black;
    black.setColor(BLACK);
    black.setAntiAlias(true);

    red.setDither(true);
    red.setAntiAlias(true);
    red.setTextSize(50);
    canvas->drawString("Hering Illusion", 50, 50, red);

    for (auto count = 0; count < COUNT; ++count) {

      canvas->save();
      auto angle = 360.0 * count / COUNT;

      canvas->translate(SIZE, SIZE + START);
      canvas->rotate(angle);

      SkRect bar = SkRect::MakeXYWH(0, 0, SIZE, WIDTH);
      canvas->drawRect(bar, black);
      canvas->restore();
    }

    auto dist = SIZE / BARS - BAR;
    float x = dist / 2;
    for (auto bar = 0; bar < BARS; ++bar) {
      SkRect left = SkRect::MakeXYWH(SIZE - x - BAR, START, BAR, SIZE * 2);
      canvas->drawRect(left, red);
      SkRect right = SkRect::MakeXYWH(SIZE + x, START, BAR, SIZE * 2);
      canvas->drawRect(right, red);
      x += (dist + BAR);
    }
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion4GM; )

