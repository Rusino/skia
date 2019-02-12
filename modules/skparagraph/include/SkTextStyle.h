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

#include "flutter/SkDartTypes.h"
#include "SkFontStyle.h"
#include "flutter/SkTextShadow.h"
#include "SkColor.h"
#include "SkPaint.h"
#include "SkFont.h"

class SkTextStyle {

  public:

    SkTextStyle();

    bool operator==(const SkTextStyle& rhs) const {
        return this->fFontHeight == rhs.fFontHeight &&
            this->fLetterSpacing == rhs.fLetterSpacing &&
            this->fFontStyle == rhs.fFontStyle &&
            this->fFontFamily == rhs.fFontFamily &&
            this->fBackground == rhs.fBackground &&
            this->fForeground == rhs.fForeground &&
            this->fColor == rhs.fColor &&
            this->fTextShadows == rhs.fTextShadows &&
            this->fDecoration == rhs.fDecoration;
    }

    bool equals(const SkTextStyle& other) const;

    // Colors
    bool hasForeground() const { return fHasForeground; }
    bool hasBackground() const { return fHasBackground; }
    SkPaint getForeground() const { return fForeground; }
    SkPaint getBackground() const { return fBackground; }
    SkColor getColor() const { return fColor; }

    void setColor(SkColor color) { fColor = color; }
    void setForegroundColor(SkPaint paint) {
        fHasForeground = true;
        fForeground = paint;
    }
    void setBackgroundColor(SkColor color) {
        fHasBackground = true;
        fBackground.setColor(color);
    }

    // Decorations
    SkTextDecoration getDecoration() const { return fDecoration; }
    SkColor getDecorationColor() const { return fDecorationColor; }
    SkTextDecorationStyle
    getDecorationStyle() const { return fDecorationStyle; }
    SkScalar
    getDecorationThicknessMultiplier() const { return fDecorationThicknessMultiplier; }
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
    SkFontStyle getFontStyle() const { return fFontStyle; }
    void setFontStyle(SkFontStyle fontStyle) { fFontStyle = fontStyle; }

    // Shadows
    size_t getShadowNumber() const { return fTextShadows.size(); }
    std::vector<SkTextShadow> getShadows() const { return fTextShadows; }
    void addShadow(SkTextShadow shadow) { fTextShadows.emplace_back(shadow); }
    void resetShadows() { fTextShadows.clear(); }

    void getFontMetrics(SkFontMetrics& metrics) const {
        SkFont font(fTypeface, fFontSize);
        font.getMetrics(&metrics);
    }

    SkScalar getFontSize() const { return fFontSize; }
    void setFontSize(SkScalar size) { fFontSize = size; }

    std::string getFontFamily() const { return fFontFamily; };
    void setFontFamily(const std::string& family) { fFontFamily = family; }

    void setHeight(SkScalar height) { fFontHeight = height; }
    void setLetterSpacing(SkScalar letterSpacing) {
        fLetterSpacing = letterSpacing;
    }
    void setWordSpacing(SkScalar wordSpacing) { fWordSpacing = wordSpacing; }

    sk_sp<SkTypeface> getTypeface() const;
    void setTypeface(sk_sp<SkTypeface> typeface) { fTypeface = typeface; }

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

