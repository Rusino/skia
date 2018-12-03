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

#define MYSTIC 0xffa0efe1
#define ORANGE 0xfff98f10
#define MAGENTA 0xfffa12e7

#define REPEAT 10
#define START 100
#define STEP 10


// Blue/Green Optical Illusion
class Illusion3GM : public skiagm::GM {
 public:
  Illusion3GM() {
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion3");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint mystic;
    mystic.setColor(MYSTIC);
    mystic.setAntiAlias(true);

    SkPaint orange;
    orange.setColor(ORANGE);
    orange.setAntiAlias(true);

    SkPaint magenta;
    magenta.setColor(MAGENTA);
    magenta.setAntiAlias(true);

    canvas->save();

    mystic.setDither(true);
    mystic.setAntiAlias(true);
    mystic.setTextSize(50);
    canvas->drawString("Blue/Green Optical Illusion", 50, 50, mystic);

    bool is_mystic = true;
    for (auto y = START; y < H; y += STEP) {

      SkRect left = SkRect::MakeXYWH(0, y, W / 2, STEP);
      SkRect right = SkRect::MakeXYWH(W / 2, y, W / 2, STEP);

      if (y < (H + START) / 2) {
        if (is_mystic) {
          canvas->drawRect(left, mystic);
          canvas->drawRect(right, orange);
        } else {
          canvas->drawRect(left, orange);
          canvas->drawRect(right, magenta);
        }
      } else {
        if (is_mystic) {
          canvas->drawRect(right, mystic);
          canvas->drawRect(left, magenta);
        } else {
          canvas->drawRect(right, magenta);
          canvas->drawRect(left, orange);
        }
      }
      is_mystic = !is_mystic;
    }

    canvas->restore();
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion3GM; )

