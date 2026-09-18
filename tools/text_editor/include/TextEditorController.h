/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TextEditorController_DEFINED
#define TextEditorController_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "tools/skui/InputState.h"
#include "tools/skui/Key.h"
#include "tools/skui/ModifierKey.h"
#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/FormattedParagraph.h"
#include "tools/text_editor/include/ParagraphSpatialIndex.h"
#include "tools/text_editor/include/ShapedParagraph.h"
#include "tools/text_editor/include/UnicodeParagraph.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace skia::text_editor {

/**
 * TextEditorController maintains the mutable document text, styling,
 * selection state, and orchestrates full immutable rebuilds of Layers 1-4.
 */
class TextEditorController {
public:
    static std::unique_ptr<TextEditorController> Make(
        std::string initial_text,
        SkFont default_font,
        SkColor4f text_color = SkColor4f{0, 0, 0, 1},
        LayoutConstraints constraints = LayoutConstraints{});

    virtual ~TextEditorController() = default;

    // Document State Queries
    virtual std::string_view text() const = 0;
    virtual const EditorSelection& selection() const = 0;
    virtual const ParagraphSpatialIndex& spatial_index() const = 0;
    virtual const LayoutConstraints& constraints() const = 0;

    // Mutating Operations
    virtual void insertText(std::string_view utf8_text) = 0;
    virtual void deleteBackward() = 0;
    virtual void deleteForward() = 0;

    // Selection & Navigation Operations
    virtual void setSelection(CaretPosition anchor, CaretPosition focus) = 0;
    virtual void collapseTo(CaretPosition pos) = 0;
    virtual void selectAll() = 0;
    virtual void moveCaret(CursorDirection dir, MovementGranularity gran, NavigationMode mode, bool select) = 0;
    virtual void moveCaretToPoint(SkScalar x, SkScalar y, bool select) = 0;
    virtual void selectWordAtPoint(SkScalar x, SkScalar y) = 0;

    // Layout configuration
    virtual void setConstraints(LayoutConstraints constraints) = 0;

    // Input Event Handling (Decoupled from native windowing)
    virtual bool handleKey(skui::Key key, skui::InputState state, skui::ModifierKey modifiers) = 0;
    virtual bool handleChar(SkUnichar c, skui::ModifierKey modifiers) = 0;
};

} // namespace skia::text_editor

#endif // TextEditorController_DEFINED
