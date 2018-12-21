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

#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "SkFontMgr.h"
#include "SkRefCnt.h"
#include "SkTextStyle.h"
#include "SkFontManager.h"

class SkFontCollection : public std::enable_shared_from_this<SkFontCollection> {
 public:
  SkFontCollection();

  ~SkFontCollection();

  size_t GetFontManagersCount() const;

  void SetAssetFontManager(std::shared_ptr<SkFontManager> fontManager);
  void SetDynamicFontManager(std::shared_ptr<SkFontManager> fontManager);
  void SetTestFontManager(std::shared_ptr<SkFontManager> fontManager);

  sk_sp<SkTypeface> findTypeface(SkTextStyle& textStyle);

  void DisableFontFallback();

 private:
  struct FamilyKey {
    FamilyKey(const std::string& family, const std::string& loc)
        : font_family(family), locale(loc) {}

    std::string font_family;
    std::string locale;

    bool operator==(const FamilyKey& other) const;

    struct Hasher {
      size_t operator()(const FamilyKey& key) const;
    };
  };

  bool _enableCallback;
  sk_sp<SkFontMgr> _defaultFontManager;
  std::shared_ptr<SkFontManager> _assetFontManager;
  std::shared_ptr<SkFontManager> _dynamicFontManager;
  std::shared_ptr<SkFontManager> _testFontManager;
  std::unordered_map<FamilyKey,
                     sk_sp<SkTypeface>,
                     FamilyKey::Hasher>
                    _typefaces;
  std::vector<std::shared_ptr<SkFontManager>> GetFontManagerOrder() const;
};
