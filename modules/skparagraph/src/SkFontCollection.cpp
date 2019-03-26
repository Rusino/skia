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

    return fFontFamily == other.fFontFamily &&
        fLocale == other.fLocale &&
        fFontStyle == other.fFontStyle;
}

size_t SkFontCollection::FamilyKey::Hasher::operator()(
    const SkFontCollection::FamilyKey& key) const {

    return std::hash<std::string>()(key.fFontFamily) ^
        std::hash<std::string>()(key.fLocale) ^
        std::hash<uint32_t>()(key.fFontStyle.weight()) ^
        std::hash<uint32_t>()(key.fFontStyle.slant());
}

SkFontCollection::SkFontCollection()
    : fEnableFontFallback(true), fDefaultFontManager(SkFontMgr::RefDefault()) {
}

SkFontCollection::~SkFontCollection() = default;

size_t SkFontCollection::getFontManagersCount() const {

    return this->getFontManagerOrder().size();
}

void SkFontCollection::setAssetFontManager(sk_sp<SkFontMgr> font_manager) {

    fAssetFontManager = font_manager;
}

void SkFontCollection::setDynamicFontManager(sk_sp<SkFontMgr> font_manager) {

    fDynamicFontManager = font_manager;
}

void SkFontCollection::setTestFontManager(sk_sp<SkFontMgr> font_manager) {

    fTestFontManager = font_manager;
}

// Return the available font managers in the order they should be queried.
std::vector<sk_sp<SkFontMgr>> SkFontCollection::getFontManagerOrder() const {

    std::vector<sk_sp<SkFontMgr>> order;
    if (fDynamicFontManager) {
        order.push_back(fDynamicFontManager);
    }
    if (fAssetFontManager) {
        order.push_back(fAssetFontManager);
    }
    if (fTestFontManager) {
        order.push_back(fTestFontManager);
    }
    if (fDefaultFontManager && fEnableFontFallback) {
        order.push_back(fDefaultFontManager);
    }
    return order;
}

SkTypeface* SkFontCollection::findTypeface(SkTextStyle& textStyle) {

    // Look inside the font collections cache first
    FamilyKey
        familyKey(textStyle.getFirstFontFamily(), "en", textStyle.getFontStyle());
    auto found = fTypefaces.find(familyKey);
    if (found) {
        textStyle.setTypeface(*found);
        return SkRef(found->get());
    }

    sk_sp<SkTypeface> typeface = nullptr;
    int n = 0;
    for (auto manager : this->getFontManagerOrder()) {
        ++n;
        SkFontStyleSet
            * set = manager->matchFamily(textStyle.getFirstFontFamily().c_str());
        if (nullptr == set || set->count() == 0) {
            continue;
        }

        for (int i = 0; i < set->count(); ++i) {
            set->createTypeface(i);
        }

        sk_sp<SkTypeface> match(set->matchStyle(textStyle.getFontStyle()));
        if (match) {
            typeface = std::move(match);
            break;
        }
    }

    if (nullptr == typeface) {
        typeface.reset(fDefaultFontManager->matchFamilyStyle(DEFAULT_FONT_FAMILY,
                                                             SkFontStyle()));
    } else {
        fTypefaces.set(familyKey, typeface);
    }

    textStyle.setTypeface(typeface);

    return SkRef(typeface.get());
}

void SkFontCollection::disableFontFallback() {

    fEnableFontFallback = false;
}

