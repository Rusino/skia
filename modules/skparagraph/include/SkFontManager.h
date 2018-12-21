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
#include "SkFontMgr.h"
#include "SkFontProvider.h"

class SkFontManager : public SkFontMgr {
 public:
    SkFontManager(SkFontProvider*&& provider);

  ~SkFontManager() override;


  SkFontProvider* GetFontProvider() { return _provider.get(); };

 protected:
  SkFontStyleSet* onMatchFamily(const char familyName[]) const override;

  std::unique_ptr<SkFontProvider> _provider;

 private:
  int onCountFamilies() const override;

  void onGetFamilyName(int index, SkString* familyName) const override;

  SkFontStyleSet* onCreateStyleSet(int index) const override;

  SkTypeface* onMatchFamilyStyle(const char familyName[], const SkFontStyle&) const override;

  SkTypeface* onMatchFamilyStyleCharacter(const char familyName[],
                                          const SkFontStyle&,
                                          const char* bcp47[],
                                          int bcp47Count,
                                          SkUnichar character) const override;

  SkTypeface* onMatchFaceStyle(const SkTypeface*, const SkFontStyle&) const override;

  sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData>, int ttcIndex) const override;

  sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset>,
                                          int ttcIndex) const override;

  sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset>,
                                         const SkFontArguments&) const override;

  sk_sp<SkTypeface> onMakeFromFile(const char path[], int ttcIndex) const override;

  sk_sp<SkTypeface> onLegacyMakeTypeface(const char familyName[], SkFontStyle) const override;
};
