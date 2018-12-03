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
#define BLACK SK_ColorBLACK
#define GRAY SK_ColorGRAY

#define REPEAT 10
#define START 100
#define BORDER 4

const int SIZE = W / REPEAT / 2 - BORDER;
const float SHIFT = SIZE * 0.5;
const int STEP = SIZE + BORDER;

// Parallel Lines Optical Illusion
class Illusion2GM : public skiagm::GM {
 public:
  Illusion2GM() {
    this->setBGColor(WHITE);
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion2");
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

    canvas->save();

    gray.setDither(true);
    gray.setAntiAlias(true);
    gray.setTextSize(50);
    canvas->drawString("Parallel Lines Optical Illusion", 50, 50, gray);

    gray.setStrokeCap(SkPaint::kRound_Cap);
    gray.setStrokeWidth(BORDER);

    int dir = SHIFT;
    int shift = 0;

    SkRect line = SkRect::MakeXYWH(0, START, W, BORDER);
    canvas->drawRect(line, gray);

    for (auto y = START; y < H; y += STEP) {

      for (auto x = 0; x < W; x += STEP * 2) {

        SkRect grey_left = SkRect::MakeXYWH(x + shift, y + BORDER, BORDER, SIZE);
        canvas->drawRect(grey_left, gray);

        SkRect black_rect = SkRect::MakeXYWH(x + shift + BORDER, y + BORDER, SIZE, SIZE);
        canvas->drawRect(black_rect, black);

        SkRect grey_right = SkRect::MakeXYWH(x + shift + STEP, y + BORDER, BORDER, SIZE);
        canvas->drawRect(grey_right, gray);
      }

      SkRect line = SkRect::MakeXYWH(0, y + STEP, W, BORDER);
      canvas->drawRect(line, gray);

      shift += dir;
      if (shift > SIZE || shift < 0) {
        dir = - dir;
        shift += dir * 2;
      }
    }

    canvas->restore();
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion2GM; )

