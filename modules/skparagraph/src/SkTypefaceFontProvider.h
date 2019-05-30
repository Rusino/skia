/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#pragma once

#include <include/private/SkTArray.h>
#include <include/private/SkTHash.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "include/core/SkFontMgr.h"

class SkTypefaceFontStyleSet : public SkFontStyleSet {
 public:
  explicit SkTypefaceFontStyleSet(const SkString& familyName);

  int count() override;
  void getStyle(int index, SkFontStyle*, SkString* name) override;
  SkTypeface* createTypeface(int index) override;
  SkTypeface* matchStyle(const SkFontStyle& pattern) override;

  SkString getFamilyName() const { return fFamilyName; }
  SkString getAlias() const { return fAlias; }
  void appendTypeface(sk_sp<SkTypeface> typeface);

 private:
  SkTArray<sk_sp<SkTypeface>> fStyles;
  SkString fFamilyName;
  SkString fAlias;
};

class SkTypefaceFontProvider : public SkFontMgr {
 public:

  void registerTypeface(sk_sp<SkTypeface> typeface);
  void registerTypeface(sk_sp<SkTypeface> typeface, const SkString& alias);

  int onCountFamilies() const override;

  void onGetFamilyName(int index, SkString* familyName) const override;

  SkFontStyleSet* onMatchFamily(const char familyName[]) const override;

  SkFontStyleSet* onCreateStyleSet(int index) const override { return nullptr; }
  SkTypeface* onMatchFamilyStyle(const char familyName[],
                                 const SkFontStyle& style) const override { return nullptr; }
  SkTypeface* onMatchFamilyStyleCharacter(const char familyName[],
                                          const SkFontStyle& style,
                                          const char* bcp47[], int bcp47Count,
                                          SkUnichar character) const override { return nullptr; }
  SkTypeface* onMatchFaceStyle(const SkTypeface* tf,
                               const SkFontStyle& style) const override { return nullptr; }

  sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData>, int ttcIndex) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset>,
                                          int ttcIndex) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset>,
                                         const SkFontArguments&) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromFontData(std::unique_ptr<SkFontData>) const override { return nullptr; }
  sk_sp<SkTypeface> onMakeFromFile(const char path[], int ttcIndex) const override { return nullptr; }

  sk_sp<SkTypeface> onLegacyMakeTypeface(const char familyName[],
                                         SkFontStyle style) const override { return nullptr; }

 private:
  SkTArray<sk_sp<SkTypefaceFontStyleSet>> fRegisteredFamilies;
};

