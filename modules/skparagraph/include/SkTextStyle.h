/*
 * Copyright 2018 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>
#include <vector>

#include "SkDartTypes.h"
#include "SkFontStyle.h"
#include "SkTextShadow.h"
#include "SkColor.h"
#include "SkPaint.h"
#include "SkFont.h"

class SkTextStyle {

 public:

  SkTextStyle();

  bool equals(const SkTextStyle& other) const;

  // Colors
  bool hasForeground() { return _hasForeground; }
  bool hasBackground() { return _hasBackground; }
  SkPaint getForeground() { return _foreground; }
  SkPaint getBackground() { return _background; }
  SkColor getColor() { return _color; }

  void setColor(SkColor color) { _color = color; }
  void setForegroundColor(SkPaint paint) {
    _hasForeground = true;
    _foreground = paint;
  }
  void setBackgroundColor(SkColor color) {
    _hasBackground = true;
    _background = SkPaint();
    _background.setColor(color);
  }

  // Decorations
  SkTextDecoration getDecoration() { return _decoration; }
  SkColor getDecorationColor() { return _decorationColor; }
  SkTextDecorationStyle getDecorationStyle() { return _decorationStyle; }
  SkScalar getDecorationThicknessMultiplier() { return _decorationThicknessMultiplier; }
  void setDecoration(SkTextDecoration decoration) { _decoration = decoration; }
  void setDecorationStyle(SkTextDecorationStyle style) { _decorationStyle = style; }
  void setDecorationColor(SkColor color) { _decorationColor = color; }
  void setDecorationThicknessMultiplier(SkScalar m) { _decorationThicknessMultiplier = m; }

  // Weight/Width/Slant
  SkFontStyle getFontStyle() { return _fontStyle; }
  void setFontStyle(SkFontStyle fontStyle) { _fontStyle = fontStyle; }

  // Shadows
  size_t getShadowNumber() { return _textShadows.size(); }
  std::vector<SkTextShadow> getShadows() { return _textShadows; }
  void addShadow(SkTextShadow shadow) { _textShadows.emplace_back(shadow); }
  void resetShadows() { _textShadows.clear(); }

  void getFontMetrics(SkFontMetrics& metrics) { _font.getMetrics(&metrics); }

  SkScalar getFontSize() { return _fontSize; }
  void setFontSize(SkScalar size) { _fontSize = size; }

  std::string getFontFamily() { return _fontFamily; };
  void setFontFamily(const std::string& family) { _fontFamily = family; }

  void setHeight(SkScalar height) { _height = height; }
  void setLetterSpacing(SkScalar letterSpacing) { _letterSpacing = letterSpacing; }
  void setWordSpacing(SkScalar wordSpacing) { _wordSpacing = wordSpacing; }

  sk_sp<SkTypeface> getTypeface();
  void setTypeface(sk_sp<SkTypeface> typeface) { _typeface = typeface; }


 private:
  SkTextDecoration _decoration;
  SkColor _decorationColor;
  SkTextDecorationStyle _decorationStyle;
  SkScalar _decorationThicknessMultiplier;

  SkFont _font;

  SkFontStyle _fontStyle;

  std::string _fontFamily;
  SkScalar _fontSize;

  SkScalar _height;
  std::string _locale;
  SkScalar _letterSpacing;
  SkScalar _wordSpacing;

  SkColor _color;
  bool _hasBackground;
  SkPaint _background;
  bool _hasForeground;
  SkPaint _foreground;

  std::vector<SkTextShadow> _textShadows;

  sk_sp<SkTypeface> _typeface;
};

