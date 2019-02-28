/*
 * Copyright 2019 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

#include <vector>
#include "SkTArray.h"

template <typename T>
class SkArraySpan {
 public:
  constexpr SkArraySpan() : fArray{nullptr}, fStart{0}, fEnd(0) {}
  constexpr SkArraySpan(SkTArray<T, true>& array, size_t start, size_t end)
      : fArray{&array}, fStart{start}, fEnd(end) {}
  constexpr SkArraySpan(SkTArray<T, true>& array, T* start, size_t end)
      : fArray{&array}, fStart{(size_t)(start - array.begin())}, fEnd((size_t)(start - array.begin()) + end) {}
  constexpr SkArraySpan(const SkArraySpan& o) = default;
  constexpr SkArraySpan& operator=(const SkArraySpan& that) {
    fArray = that.fArray;
    fStart = that.fStart;
    fEnd = that.fEnd;
    return *this;
  }
  constexpr T& operator [] (size_t i) { return fArray[i]; }
  constexpr T* begin() const { return fArray->begin() + fStart; }
  constexpr T* end() const { return fArray->begin() + fEnd; }
  constexpr T* data() const { return fArray->begin() + fStart; }
  constexpr size_t size() const { return fEnd - fStart; }
  constexpr bool empty() const { return fStart == fEnd; }
  constexpr size_t size_bytes() const { return (fEnd - fStart) * sizeof(T); }
  constexpr SkArraySpan<const T> toConst() const { return SkArraySpan<const T>{fArray, fStart, fEnd}; }

 private:
  SkTArray<T, true>* fArray;
  size_t fStart;
  size_t fEnd;
};
