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
#include <list>
#include <algorithm>

#include "flutter/SkParagraphBuilder.h"
#include "SkParagraphStyle.h"
#include "SkPaint.h"
#include "SkSpan.h"

SkParagraphBuilder::SkParagraphBuilder(
    SkParagraphStyle style,
    sk_sp<SkFontCollection> font_collection)
    : fFontCollection(std::move(font_collection)) {
    SetParagraphStyle(style);
}

SkParagraphBuilder::~SkParagraphBuilder() = default;

void SkParagraphBuilder::SetParagraphStyle(const SkParagraphStyle& style) {
    fParagraphStyle = style;
    auto& textStyle = fParagraphStyle.getTextStyle();
    fFontCollection->findTypeface(textStyle);
    fTextStyles.push(textStyle);
    fStyledBlocks.emplace_back(fUtf8.size(), fUtf8.size(), textStyle);
}

void SkParagraphBuilder::PushStyle(const SkTextStyle& style) {
    EndRunIfNeeded();

    fTextStyles.push(style);
    if (!fStyledBlocks.empty() && fStyledBlocks.back().end == fUtf8.size()
        && fStyledBlocks.back().textStyle == style) {
        // Just continue with the same style
    } else {
        // Resolve the new style and go with it
        auto& textStyle = fTextStyles.top();
        fFontCollection->findTypeface(textStyle);
        fStyledBlocks.emplace_back(fUtf8.size(), fUtf8.size(), textStyle);
    }
}

void SkParagraphBuilder::Pop() {

    EndRunIfNeeded();
    if (fTextStyles.size() > 1) {
        fTextStyles.pop();
    } else {
        // In this case we use paragraph style and skip Pop operation
        SkDebugf("SkParagraphBuilder.Pop() called too many times.\n");
    }

    auto top = fTextStyles.top();
    fStyledBlocks.emplace_back(fUtf8.size(), fUtf8.size(), top);
}

SkTextStyle SkParagraphBuilder::PeekStyle() {

    EndRunIfNeeded();
    if (!fTextStyles.empty()) {
        return fTextStyles.top();
    } else {
        SkDebugf("SkParagraphBuilder._styles is empty.\n");
        return fParagraphStyle.getTextStyle();
    }
}

void SkParagraphBuilder::AddText(const std::u16string& text) {

    icu::UnicodeString unicode;
    unicode.setTo((UChar*) text.data());
    unicode.toUTF8String(fUtf8);
}

void SkParagraphBuilder::AddText(const std::string& text) {

    icu::UnicodeString unicode;
    unicode.setTo(text.data());
    unicode.toUTF8String(fUtf8);
}

void SkParagraphBuilder::AddText(const char* text) {
    icu::UnicodeString unicode;
    unicode.setTo(text);
    unicode.toUTF8String(fUtf8);
}

void SkParagraphBuilder::EndRunIfNeeded() {
    if (fStyledBlocks.empty()) {
        return;
    }

    auto& last = fStyledBlocks.back();
    if (last.start == fUtf8.size()) {
        fStyledBlocks.pop_back();
    } else {
        last.end = fUtf8.size();
    }
}

std::unique_ptr<SkParagraph> SkParagraphBuilder::Build() {
    EndRunIfNeeded();
    return std::make_unique<SkParagraph>(fUtf8, fParagraphStyle, fStyledBlocks);
}

