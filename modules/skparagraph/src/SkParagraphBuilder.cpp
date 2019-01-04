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

/*
std::unique_ptr<SkParagraph> SkParagraphBuilder::BuildSk() {
  runs_.EndRunIfNeeded(text_.size());

  // Get the first run style - temporary hack
  SkColor foreground = SK_ColorWHITE;
  SkColor background;
  SkFontStyle::Weight weight = (SkFontStyle::Weight)_paragraphStyle.font_weight;

  TextStyle style;
  for (size_t i = 0; i < runs_.size(); ++i) {
    auto run = runs_.GetRun(i);
    if (run.start == run.end) {
      continue;
    }

    style = run.style;
    if (style.has_foreground) {
      foreground = style.foreground.getColor();
    } else {
      foreground = style.color;
    }
    if (style.has_background) {
      background = style.background.getColor();
    }

    weight = (SkFontStyle::Weight)style.font_weight;
  }

  // Get the typeface  for the run - temporary hack
  uint32_t language_list_id =
      style.locale.empty()
      ? minikin::FontLanguageListCache::kEmptyListId
      : minikin::FontStyle::registerLanguageList(style.locale);
  auto fontStyle = minikin::FontStyle(language_list_id, 0, weight, _paragraphStyle.font_style == FontStyle::italic);

  std::string locale;
  if (!style.locale.empty()) {
    uint32_t language_list_id =
        minikin::FontStyle::registerLanguageList(style.locale);
    const minikin::FontLanguages& langs =
        minikin::FontLanguageListCache::getById(language_list_id);
    if (langs.size()) {
      locale = langs[0].getString();
    }
  }
  std::shared_ptr<minikin::FontCollection> collection = font_collection_->GetMinikinFontCollectionForFamily(style.font_family, locale);
  minikin::FakedFont faked_font = collection->baseFontFaked(fontStyle);
  sk_sp<SkTypeface> typeface = static_cast<FontSkia*>(faked_font.font)->GetSkTypeface();

  std::unique_ptr<SkParagraph> paragraph = std::make_unique<SkParagraph>(typeface);
  paragraph->SetText(std::move(text_));
  paragraph->SetParagraphStyle(
      foreground,
      background,
      _paragraphStyle.font_size,
      std::string(style.font_family.c_str()),
      static_cast<SkFontStyle::Weight>(_paragraphStyle.font_weight),
      (_paragraphStyle.text_direction == txt::TextDirection::ltr ? ::TextDirection::ltr : ::TextDirection::rtl),
      _paragraphStyle.max_lines);

  return paragraph;
}
*/
