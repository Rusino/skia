/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TextEditorViewModel_DEFINED
#define TextEditorViewModel_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "tools/skui/InputState.h"
#include "tools/skui/Key.h"
#include "tools/skui/ModifierKey.h"
#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/TextDocument.h"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace skia::text_editor {

/**
 * TextEditorViewModel (ViewModel in MVVM):
 * Concrete presentation coordinator owning session state (selection, caret blinking, viewport scroll offset),
 * translating user intents/commands into Model mutations, and preparing pre-transformed
 * geometry for the View.
 *
 * Guaranteed Invariants:
 * 1. Synchronous Caret Invariant: After any command that mutates the document, selection
 *    and caret coordinates are immediately validated against the new spatial index.
 * 2. Hot-Path Zero-Allocation Invariant: Caret movement and hit-testing queries execute
 *    without allocating memory on the heap.
 * 3. Atomic Replacement Invariant: Inserting text over an active non-collapsed selection
 *    strictly executes an atomic replace in the document (single pipeline rebuild).
 * 4. Coordinate Separation Invariant: Methods with screenX/screenY explicitly take viewport
 *    pixels and translate them via scroll offset before querying document geometry.
 * 5. Headless Testability: TextEditorViewModel has zero dependence on SkCanvas or OS windows.
 */
class TextEditorViewModel {
public:
    using RedrawCallback = std::function<void()>;

    TextEditorViewModel(std::unique_ptr<TextDocument> document,
                        NavigationMode default_nav_mode = NavigationMode::kTextLogical);

    TextEditorViewModel(std::string initial_text,
                        SkFont default_font,
                        SkColor4f text_color = SkColor4f{0, 0, 0, 1},
                        LayoutConstraints constraints = LayoutConstraints{},
                        NavigationMode default_nav_mode = NavigationMode::kTextLogical);

    ~TextEditorViewModel() = default;

    // Model Access (Fully Inlined)
    const TextDocument& document() const { return *fDocument; }
    TextDocument& document() { return *fDocument; }

    // View Observation
    void setOnRedrawCallback(RedrawCallback cb) { fOnRedraw = std::move(cb); }

    // Presentation State Queries (Fully Inlined)
    const EditorSelection& selection() const { return fSelection; }
    SkPoint scrollOffset() const { return fScrollOffset; }
    void setScrollOffset(SkPoint offset) { fScrollOffset = offset; notifyRedraw(); }
    bool isCaretVisible() const { return fCaretVisible; }
    void setCaretVisible(bool visible) { fCaretVisible = visible; notifyRedraw(); }
    NavigationMode navigationMode() const { return fNavMode; }
    void setNavigationMode(NavigationMode mode) { fNavMode = mode; }

    // High-Level User Commands (Actions)
    void insertText(std::string_view utf8_text);
    void deleteBackward(MovementGranularity gran = MovementGranularity::kGrapheme);
    void deleteForward(MovementGranularity gran = MovementGranularity::kGrapheme);
    void moveCaret(CursorDirection dir, MovementGranularity gran, NavigationMode mode, bool select);
    void moveCaretToPoint(SkScalar screenX, SkScalar screenY, bool select);
    void selectAll();
    void selectWordAtPoint(SkScalar screenX, SkScalar screenY);
    void setSelection(CaretPosition anchor, CaretPosition focus);
    void collapseTo(CaretPosition pos);

    // Viewport & Scrolling Management
    void ensureCaretVisible(const SkRect& viewportBounds);

    // Low-Level Input Event Adapters (Translates OS events to ViewModel commands)
    bool handleKey(skui::Key key, skui::InputState state, skui::ModifierKey modifiers);
    bool handleChar(SkUnichar c, skui::ModifierKey modifiers);

    // View-Ready Geometry Queries (Coordinates translated for screen scroll offset)
    SkRect screenCaretRect() const;
    std::vector<SkRect> screenSelectionRects() const;

    // Zero-allocation streaming visitor for View paint layer (screen/viewport coordinates)
    void visitScreenRuns(const SkRect& screenClip, RenderRunVisitor visitor) const;

private:
    void notifyRedraw() {
        if (fOnRedraw) {
            fOnRedraw();
        }
    }
    void updateCursorPosition(size_t index);

    std::unique_ptr<TextDocument> fDocument;
    EditorSelection fSelection;
    SkPoint fScrollOffset{0, 0};
    bool fCaretVisible{true};
    NavigationMode fNavMode{NavigationMode::kTextLogical};
    RedrawCallback fOnRedraw;
};

} // namespace skia::text_editor

#endif // TextEditorViewModel_DEFINED
