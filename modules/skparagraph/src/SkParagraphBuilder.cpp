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
#include <list>

#include "SkParagraphBuilder.h"
#include "SkParagraphStyle.h"
#include "SkPaint.h"
#include "unicode/utf16.h"
#include "unicode/unistr.h"

SkParagraphBuilder::SkParagraphBuilder(
    SkParagraphStyle style,
    std::shared_ptr<SkFontCollection> font_collection)
    : _fontCollection(std::move(font_collection)) {
  SetParagraphStyle(style);
}

SkParagraphBuilder::~SkParagraphBuilder() = default;

void SkParagraphBuilder::SetParagraphStyle(const SkParagraphStyle& style) {
  _style = style;

  auto& textStyle = _style.getTextStyle();
  _fontCollection->findTypeface(textStyle);
  _runs.emplace_back(_text.size(), _text.size(), textStyle);
}

void SkParagraphBuilder::PushStyle(const SkTextStyle& style) {
  EndRunIfNeeded();

  auto textStyle = style;
  _fontCollection->findTypeface(textStyle);
  _styles.push(textStyle);
  _runs.emplace_back(_text.size(),_text.size(), textStyle);
}

SkTextStyle SkParagraphBuilder::PeekStyle() {
  EndRunIfNeeded();
  if (_styles.empty()) {
    return _style.getTextStyle();
  }
  return _styles.top();
}

void SkParagraphBuilder::Pop() {

  EndRunIfNeeded();
  if (_styles.empty()) {
    return;
  }

  _styles.pop();
  auto top = _styles.empty() ? _style.getTextStyle() : _styles.top();
  _runs.emplace_back(_text.size(), _text.size(), top);
}

void SkParagraphBuilder::AddText(const std::u16string& text) {

  _text.insert(_text.end(), text.begin(), text.end());
}

void SkParagraphBuilder::AddText(const std::string& text) {
  auto icu_text = icu::UnicodeString::fromUTF8(text);
  std::u16string u16_text(icu_text.getBuffer(),
                          icu_text.getBuffer() + icu_text.length());
  AddText(u16_text);
}

void SkParagraphBuilder::AddText(const char* text) {
  auto icu_text = icu::UnicodeString::fromUTF8(text);
  std::u16string u16_text(icu_text.getBuffer(),
                          icu_text.getBuffer() + icu_text.length());
  AddText(u16_text);
}

void SkParagraphBuilder::EndRunIfNeeded() {
  if (_runs.empty()) {
    return;
  }
  auto& last = _runs.back();
  if (last.start == _text.size()) {
    _runs.pop_back();
  } else {
    last.end = _text.size();
  }
}

std::unique_ptr<SkParagraph> SkParagraphBuilder::Build() {
  EndRunIfNeeded();

  std::unique_ptr<SkParagraph> paragraph = std::make_unique<SkParagraph>();
  paragraph->SetText(std::move(_text));
  paragraph->SetStyles(std::move(_runs));
  paragraph->SetParagraphStyle(_style);
  paragraph->SetFontCollection(_fontCollection);

  return paragraph;
}

