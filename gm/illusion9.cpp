/*
 * Copyright 2014 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include "gm.h"
#include "sk_tool_utils.h"
#include "SkCanvas.h"
#include "SkPath.h"
#include "SkAnimTimer.h"
#include "SkGradientShader.h"

#include "Sk1DPathEffect.h"
#include "Sk2DPathEffect.h"
#include "SkCornerPathEffect.h"
#include "SkDashPathEffect.h"
#include "SkDiscretePathEffect.h"
#include "SkOpPathEffect.h"

#define W   800
#define H   600

#define BROWN 0xffbbaa84
#define VIOLET 0xff1f0f7f
#define WHITE SK_ColorWHITE

#define START 100
#define RADIUS 25
#define DISTANCE 15
#define SCALE 1.3

const std::vector<int> points = { 1, 0, 0, -1, 2, -1, 3, 0, 2, 1, 0, 1 };
const std::vector<int> revert = { 2, 0, 3, -1, 1, -1, 0, 0, 1, 1, 3, 1 };

// Moving Illusion
class Illusion9GM : public skiagm::GM {
 public:
  Illusion9GM() {
    this->setBGColor(BROWN);
  }

 protected:

  SkString onShortName() override {
    return SkString("illusion9");
  }

  SkISize onISize() override {
    return SkISize::Make(W, H);
  }

  void modify_paint(SkPaint* paint, SkScalar radius, const std::vector<int>& points) {

    SkScalar unit = radius / 2;

    SkPath path;
    path.moveTo(points[0] * unit, points[1] * unit);
    for (unsigned i = 2; i < points.size(); i += 2) {
      path.lineTo(points[i] * unit, points[i + 1] * unit);
    }
    path.close();

    auto outer = SkPath1DPathEffect::Make(path, unit * 3, 0, SkPath1DPathEffect::kMorph_Style);
    paint->setPathEffect(outer);
  }

  void drawSmallFigure(SkCanvas* canvas, SkScalar x, SkScalar y) {

    SkPaint white;
    white.setColor(WHITE);
    white.setStyle(SkPaint::kStroke_Style);
    white.setAntiAlias(true);

    SkPaint violet;
    violet.setAntiAlias(true);
    violet.setStyle(SkPaint::kStroke_Style);
    violet.setColor(VIOLET);

    SkScalar radius = RADIUS;
    for (int i = 1; i <= 3; ++i) {
      auto factor = 2 * 3.1416926 * radius / 30;
      modify_paint(&violet, factor, points);
      white.setStrokeWidth(factor);
      canvas->drawCircle(x, y, radius, white);
      canvas->drawCircle(x, y, radius, violet);
      radius += factor * pow(SCALE, i) + DISTANCE;
    }
  }


  void drawBigFigure(SkCanvas* canvas, SkScalar x, SkScalar y) {

    SkPaint white;
    white.setColor(WHITE);
    white.setStyle(SkPaint::kStroke_Style);
    white.setAntiAlias(true);

    SkPaint violet;
    violet.setAntiAlias(true);
    violet.setStyle(SkPaint::kStroke_Style);
    violet.setColor(VIOLET);

    SkScalar radius = RADIUS * SCALE;
    for (int i = 1; i <= 4; ++i) {
      auto factor = 2 * 3.1416926 * radius / 30;
      modify_paint(&violet, factor, revert);
      white.setStrokeWidth(factor);
      canvas->drawCircle(x, y, radius, white);
      canvas->drawCircle(x, y, radius, violet);
      radius += factor * pow(SCALE, i) + DISTANCE;
    }
  }

  void onDraw(SkCanvas* canvas) override {

    SkPaint white;
    white.setColor(WHITE);
    white.setAntiAlias(true);

    SkPaint violet;
    violet.setColor(VIOLET);
    violet.setAntiAlias(true);

    violet.setDither(true);
    violet.setAntiAlias(true);
    violet.setTextSize(50);
    canvas->drawString("Moving Illusion", 50, 50, violet);

    const SkScalar x_step = START * 4;
    const SkScalar y_step = START * 2;
    SkScalar y = 0;
    for (auto row = 1; row <= 3; ++row) {

      SkScalar x = 0;
      x += x_step;
      y += y_step;
      drawSmallFigure(canvas, x, y);
      x += x_step;
      drawSmallFigure(canvas, x, y);

      x = START * 2;
      y += y_step;
      drawBigFigure(canvas, x, y);
      x += x_step;
      drawBigFigure(canvas, x, y);
      x += x_step;
      drawBigFigure(canvas, x, y);
    }
  }

 private:
  typedef skiagm::GM INHERITED;
};

DEF_GM( return new Illusion9GM; )

