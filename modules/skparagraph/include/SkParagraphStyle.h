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

#include <climits>
#include <string>

#include "flutter/SkDartTypes.h"
#include "SkFontStyle.h"
#include "SkTextStyle.h"

// TODO: Introduce line break strategy later
struct SkParagraphStyle {
    SkParagraphStyle();

    bool operator==(const SkParagraphStyle& rhs) const {
        return this->fLineHeight == rhs.fLineHeight &&
            this->fEllipsis == rhs.fEllipsis &&
            this->fTextDirection == rhs.fTextDirection &&
            this->fTextAlign == rhs.fTextAlign &&
            this->fDefaultTextStyle == rhs.fDefaultTextStyle;
    }

    SkTextStyle& getTextStyle() { return fDefaultTextStyle; }
    size_t getMaxLines() const { return fLinesLimit; }
    SkTextDirection getTextDirection() const { return fTextDirection; }
    std::string getEllipsis() const { return fEllipsis; }

    void setTextStyle(SkTextStyle textStyle) {
        fDefaultTextStyle = textStyle;
    }
    void setTextAlign(SkTextAlign align) { fTextAlign = align; }
    void setTextDirection(SkTextDirection direction) {
        fTextDirection = direction;
    }
    void setMaxLines(size_t maxLines) { fLinesLimit = maxLines; }
    void setEllipsis(const std::u16string& ellipsis);
    void setLineHeight(SkScalar lineHeight) { fLineHeight = lineHeight; }

    bool unlimited_lines() const;
    bool ellipsized() const;
    SkTextAlign effective_align() const;

  private:

    SkTextStyle fDefaultTextStyle;
    SkTextAlign fTextAlign;
    SkTextDirection fTextDirection;
    size_t fLinesLimit;
    std::string fEllipsis;
    SkScalar fLineHeight;
};
