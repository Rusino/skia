/*
 * Copyright 2018 Google, Inc.
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

#include "flutter/SkTextShadow.h"
#include "SkColor.h"

SkTextShadow::SkTextShadow() {}
SkTextShadow::SkTextShadow(SkColor color, SkPoint offset, double blur_radius)
    : color(color), offset(offset), blur_radius(blur_radius) {}

bool SkTextShadow::operator==(const SkTextShadow& other) const {
  if (color != other.color)
    return false;
  if (offset != other.offset)
    return false;
  if (blur_radius != other.blur_radius)
    return false;

  return true;
}

bool SkTextShadow::operator!=(const SkTextShadow& other) const {
  return !(*this == other);
}

bool SkTextShadow::hasShadow() const {
  if (!offset.isZero())
    return true;
  if (blur_radius != 0.0)
    return true;

  return false;
}
