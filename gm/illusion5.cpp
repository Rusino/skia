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

#define WHITE SK_ColorWHITE
#define GRAY SK_ColorGRAY
#define BLACK SK_ColorBLACK

#define SIZE 50.0
#define START 100
#define RADIUS 5.0

//const int NUMBER = (H - START) / SIZE;
const float WIDTH = RADIUS * 2;

// Scintillating Grid
class Illusion5GM : public skiagm::GM {
 public:
  Illusion5GM() {
    this->setBGColor(SK_ColorBLACK);
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion5");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint white;
    white.setColor(WHITE);
    white.setAntiAlias(true);

    SkPaint gray;
    gray.setColor(GRAY);
    gray.setAntiAlias(true);

    SkPaint black;
    black.setColor(BLACK);
    black.setAntiAlias(true);

    white.setDither(true);
    white.setAntiAlias(true);
    white.setTextSize(50);
    canvas->drawString("Scintillating Grid", 50, 50, white);

    canvas->save();
    for (auto y = START; y < H; y += SIZE) {

      for (auto x = 0; x < W; x += SIZE) {

        SkRect up = SkRect::MakeXYWH(x, y, SIZE, WIDTH);
        canvas->drawRect(up, gray);
        SkRect left = SkRect::MakeXYWH(x, y, WIDTH, SIZE);
        canvas->drawRect(left, gray);

        canvas->drawCircle(x + RADIUS, y + RADIUS, RADIUS, white);
      }
    }

    canvas->restore();
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion5GM; )

