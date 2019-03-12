/*
 * Copyright 2019 Google Inc.
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

#include "SkFontStyle.h"
#include "SkTextShadow.h"
#include "SkColor.h"
#include "SkPaint.h"
#include "SkFont.h"
#include "../../../src/core/SkSpan.h"

// Multiple decorations can be applied at once. Ex: Underline and overline is
// (0x1 | 0x2)
enum SkTextDecoration {
  kNone = 0x0,
  kUnderline = 0x1,
  kOverline = 0x2,
  kLineThrough = 0x4,
};

enum SkTextDecorationStyle { kSolid, kDouble, kDotted, kDashed, kWavy };

enum SkStyleType {
  Unknown,
  Text,
  Font,
  Foreground,
  Background,
  Shadow,
  Decorations
};

class SkTextStyle {

 public:

  SkTextStyle();
  ~SkTextStyle() = default;

  bool equals(const SkTextStyle& other) const;
  bool matchOneAttribute(SkStyleType styleType, const SkTextStyle& other) const;
  bool operator==(const SkTextStyle& rhs) const { return this->equals(rhs); }

  // Colors
  inline bool hasForeground() const { return fHasForeground; }
  inline bool hasBackground() const { return fHasBackground; }
  inline SkPaint getForeground() const { return fForeground; }
  inline SkPaint getBackground() const { return fBackground; }
  inline SkColor getColor() const { return fColor; }

  inline void setColor(SkColor color) { fColor = color; }
  void setForegroundColor(SkPaint paint) {
    fHasForeground = true;
    fForeground = paint;
  }
  void setBackgroundColor(SkColor color) {
    fHasBackground = true;
    fBackground.setColor(color);
  }

  // Decorations
  inline SkTextDecoration getDecoration() const { return fDecoration; }
  inline SkColor getDecorationColor() const { return fDecorationColor; }
  inline SkTextDecorationStyle getDecorationStyle() const {
    return fDecorationStyle;
  }
  inline SkScalar getDecorationThicknessMultiplier() const {
    return fDecorationThicknessMultiplier;
  }
  void setDecoration(SkTextDecoration decoration) {
    fDecoration = decoration;
  }
  void setDecorationStyle(SkTextDecorationStyle style) {
    fDecorationStyle = style;
  }
  void setDecorationColor(SkColor color) { fDecorationColor = color; }
  void setDecorationThicknessMultiplier(SkScalar m) {
    fDecorationThicknessMultiplier = m;
  }

  // Weight/Width/Slant
  inline SkFontStyle getFontStyle() const { return fFontStyle; }
  inline void setFontStyle(SkFontStyle fontStyle) { fFontStyle = fontStyle; }

  // Shadows
  inline size_t getShadowNumber() const { return fTextShadows.size(); }
  std::vector<SkTextShadow> getShadows() const { return fTextShadows; }
  void addShadow(SkTextShadow shadow) { fTextShadows.emplace_back(shadow); }
  void resetShadows() { fTextShadows.clear(); }

  void getFontMetrics(SkFontMetrics* metrics) const {
    SkFont font(fTypeface, fFontSize);
    font.getMetrics(metrics);
  }

  inline SkScalar getFontSize() const { return fFontSize; }
  inline void setFontSize(SkScalar size) { fFontSize = size; }

  inline std::string getFontFamily() const { return fFontFamily; };
  inline void setFontFamily(const std::string& family) { fFontFamily = family; }

  inline void setHeight(SkScalar height) { fFontHeight = height; }
  inline void setLetterSpacing(SkScalar letterSpacing) {
    fLetterSpacing = letterSpacing;
  }
  inline void setWordSpacing(SkScalar wordSpacing) {
    fWordSpacing = wordSpacing;
  }

  inline sk_sp<SkTypeface> getTypeface() const { return fTypeface; }
  inline void setTypeface(sk_sp<SkTypeface> typeface) { fTypeface = typeface; }

 private:
  SkTextDecoration fDecoration;
  SkColor fDecorationColor;
  SkTextDecorationStyle fDecorationStyle;
  SkScalar fDecorationThicknessMultiplier;

  SkFontStyle fFontStyle;

  std::string fFontFamily;
  SkScalar fFontSize;

  SkScalar fFontHeight;
  std::string fLocale;
  SkScalar fLetterSpacing;
  SkScalar fWordSpacing;

  SkColor fColor;
  bool fHasBackground;
  SkPaint fBackground;
  bool fHasForeground;
  SkPaint fForeground;

  std::vector<SkTextShadow> fTextShadows;

  sk_sp<SkTypeface> fTypeface;
};

// Comes from the paragraph
struct StyledText {

  StyledText(SkSpan<const char> text, SkTextStyle style)
      : fText(text), fStyle(style) {}

  bool operator==(const StyledText& rhs) const {

    return fText.begin() == rhs.fText.begin() &&
        fText.end() == rhs.fText.end() && // TODO: Can we have == on SkSpan?
        fStyle == rhs.fStyle;
  }

  SkSpan<const char> fText;
  SkTextStyle fStyle;
};