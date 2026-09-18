/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TextEditorPainter_DEFINED
#define TextEditorPainter_DEFINED

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPoint.h"
#include "tools/text_editor/include/TextEditorViewModel.h"

namespace skia::text_editor {

struct PaintOptions {
    SkColor4f selection_color = SkColor4f{0.26f, 0.52f, 0.96f, 0.35f};
    SkColor4f caret_color = SkColor4f{0.1f, 0.4f, 0.9f, 1.0f};
    bool show_caret = true;
    SkScalar caret_width = 1.5f;
    SkPoint origin = SkPoint::Make(0, 0);
};

/**
 * TextEditorPainter (View in MVVM):
 * Stateless rendering of document text lines, selection bounding boxes,
 * and carets onto an SkCanvas using presentation data from TextEditorViewModel.
 */
class TextEditorPainter {
public:
    static void Paint(SkCanvas* canvas, const TextEditorViewModel& viewModel, const PaintOptions& options = {});
};

} // namespace skia::text_editor

#endif // TextEditorPainter_DEFINED
