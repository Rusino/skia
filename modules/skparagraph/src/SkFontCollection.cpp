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

#include "SkFontCollection.h"

#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "SkTextStyle.h"

// TODO: Extract the platform dependent part
#define DEFAULT_FONT_FAMILY "sans-serif"

bool SkFontCollection::FamilyKey::operator==(
    const SkFontCollection::FamilyKey& other) const {
  return font_family == other.font_family &&
         locale == other.locale &&
         font_style == other.font_style;
}

size_t SkFontCollection::FamilyKey::Hasher::operator()(
    const SkFontCollection::FamilyKey& key) const {
  return std::hash<std::string>()(key.font_family) ^
         std::hash<std::string>()(key.locale) ^
         std::hash<uint32_t>()(key.font_style.weight()) ^
         std::hash<uint32_t>()(key.font_style.slant());
}

SkFontCollection::SkFontCollection()
  : _enableFontFallback(true) {
}

SkFontCollection::~SkFontCollection() {
}

size_t SkFontCollection::GetFontManagersCount() const {
  return GetFontManagerOrder().size();
}

void SkFontCollection::SetAssetFontManager(sk_sp<SkFontMgr> font_manager) {
  SkDebugf("SetAssetFontManager\n");
  _assetFontManager = font_manager;
}

void SkFontCollection::SetDynamicFontManager(sk_sp<SkFontMgr> font_manager) {
  SkDebugf("SetDynamicFontManager\n");
  _dynamicFontManager = font_manager;
}

void SkFontCollection::SetTestFontManager(sk_sp<SkFontMgr> font_manager) {
  SkDebugf("SetTestFontManager\n");
  _testFontManager = font_manager;
}

// Return the available font managers in the order they should be queried.
std::vector<sk_sp<SkFontMgr>> SkFontCollection::GetFontManagerOrder() const {
  std::vector<sk_sp<SkFontMgr>> order;
  if (_testFontManager)
    order.push_back(_testFontManager);
  if (_dynamicFontManager)
    order.push_back(_dynamicFontManager);
  if (_assetFontManager)
    order.push_back(_assetFontManager);
  //if (_defaultFontManager && _enableCallback)
  //  order.push_back(_defaultFontManager);
  return order;
}

// TODO: default fallback
SkTypeface* SkFontCollection::findTypeface(SkTextStyle& textStyle) {

  sk_sp<SkTypeface> typeface;

  // Look inside the font collections cache first
  FamilyKey familyKey(textStyle.getFontFamily(), "en", textStyle.getFontStyle());
  auto found = _typefaces.find(familyKey);
  if (found == nullptr) {
    for (auto manager : GetFontManagerOrder()) {
      // Cache the font collection for future queries
      SkFontStyleSet* set = manager->matchFamily(textStyle.getFontFamily().c_str());
      if (set == nullptr || set->count() == 0) {
        continue;
      }

      for (int i = 0; i < set->count(); ++i) {
        sk_sp<SkTypeface>(set->createTypeface(i));
      }

      sk_sp<SkTypeface> match(set->matchStyle(textStyle.getFontStyle()));
      if (match != nullptr) {
        typeface = match;
        break;
      }
    }

    if (typeface == nullptr) {
      // Try default;
      if (_enableFontFallback) {
        sk_sp<SkFontMgr> defaultFontManager(SkFontMgr::RefDefault());
        typeface = defaultFontManager->legacyMakeTypeface(textStyle.getFontFamily().c_str()/*DEFAULT_FONT_FAMILY*/, textStyle.getFontStyle());
        if (typeface == nullptr) {
          typeface = SkTypeface::MakeDefault();
        }
      }
      textStyle.setTypeface(typeface);
      return typeface.get();
    }
    _typefaces.set(familyKey, typeface);
  } else {
    typeface = *found;
  }

  textStyle.setTypeface(typeface);

  return typeface.get();
}

void SkFontCollection::DisableFontFallback() {
  _enableFontFallback = false;
}

