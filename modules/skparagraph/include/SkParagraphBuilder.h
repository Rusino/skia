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
#include <string>
#include <tuple>
#include <stack>

#include "SkParagraph.h"
#include "SkParagraphStyle.h"
#include "SkFontCollection.h"
#include "SkTextStyle.h"

class SkParagraphBuilder {
 public:
  SkParagraphBuilder(SkParagraphStyle style, std::shared_ptr<SkFontCollection> fontCollection);

  ~SkParagraphBuilder();

  // Push a style to the stack. The corresponding text added with AddText will
  // use the top-most style.
  void PushStyle(const SkTextStyle& style);

  // Remove a style from the stack. Useful to apply different styles to chunks
  // of text such as bolding.
  // Example:
  //   builder.PushStyle(normal_style);
  //   builder.AddText("Hello this is normal. ");
  //
  //   builder.PushStyle(bold_style);
  //   builder.AddText("And this is BOLD. ");
  //
  //   builder.Pop();
  //   builder.AddText(" Back to normal again.");
  void Pop();

  // Adds text to the builder. Forms the proper runs to use the upper-most style
  // on the style_stack_;
  void AddText(const std::u16string& text);

  // Converts to u16string before adding.
  void AddText(const std::string& text);

  // Converts to u16string before adding.
  void AddText(const char* text);

  void SetParagraphStyle(const SkParagraphStyle& style);

  // Constructs a SkParagraph object that can be used to layout and paint the text to a SkCanvas.
  std::unique_ptr<SkParagraph> Build();

 private:

  void EndRunIfNeeded();

  std::vector<uint16_t> _text;
  std::stack<SkTextStyle> _styles;
  std::vector<StyledText> _runs;
  std::shared_ptr<SkFontCollection> _fontCollection;
  SkParagraphStyle _style;

  // TODO: FML_DISALLOW_COPY_AND_ASSIGN(ParagraphBuilder);
};
