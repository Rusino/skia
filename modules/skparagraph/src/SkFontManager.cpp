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

#include <algorithm>
#include <string>

#include "SkString.h"
#include "SkTypeface.h"
#include "SkFontManager.h"

SkFontManager::SkFontManager(SkFontProvider*&& provider)
    : _provider(std::move(provider)) {
}

SkFontManager::~SkFontManager() = default;

int SkFontManager::onCountFamilies() const {
  return _provider->GetFamilyCount();
}

void SkFontManager::onGetFamilyName(int index, SkString* familyName) const {
  familyName->set(_provider->GetFamilyName(index).c_str());
}

SkFontStyleSet* SkFontManager::onCreateStyleSet(int index) const {
  SkASSERT(false);
  return nullptr;
}

SkFontStyleSet* SkFontManager::onMatchFamily(
    const char family_name_string[]) const {
  std::string family_name(family_name_string);
  return _provider->MatchFamily(family_name);
}

SkTypeface* SkFontManager::onMatchFamilyStyle(
    const char familyName[],
    const SkFontStyle& style) const {
  SkFontStyleSet* font_style_set =
      _provider->MatchFamily(std::string(familyName));
  if (font_style_set == nullptr)
    return nullptr;
  return font_style_set->matchStyle(style);
}

SkTypeface* SkFontManager::onMatchFamilyStyleCharacter(
    const char familyName[],
    const SkFontStyle&,
    const char* bcp47[],
    int bcp47Count,
    SkUnichar character) const {
  return nullptr;
}

SkTypeface* SkFontManager::onMatchFaceStyle(const SkTypeface*, const SkFontStyle&) const {
  SkASSERT(false);
  return nullptr;
}

sk_sp<SkTypeface> SkFontManager::onMakeFromData(sk_sp<SkData>, int ttcIndex) const {
  SkASSERT(false);
  return nullptr;
}

sk_sp<SkTypeface> SkFontManager::onMakeFromStreamIndex(
    std::unique_ptr<SkStreamAsset>,
    int ttcIndex) const {
  SkASSERT(false);
  return nullptr;
}

sk_sp<SkTypeface> SkFontManager::onMakeFromStreamArgs(
    std::unique_ptr<SkStreamAsset>,
    const SkFontArguments&) const {
  SkASSERT(false);
  return nullptr;
}

sk_sp<SkTypeface> SkFontManager::onMakeFromFile(const char path[], int ttcIndex) const {
  SkASSERT(false);
  return nullptr;
}

sk_sp<SkTypeface> SkFontManager::onLegacyMakeTypeface(
    const char familyName[],
    SkFontStyle) const {
  SkASSERT(false);
  return nullptr;
}
