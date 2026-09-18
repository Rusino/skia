/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/TextEditorPainter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include <vector>

namespace skia::text_editor {

void TextEditorPainter::Paint(
    SkCanvas* canvas,
    const TextEditorViewModel& viewModel,
    const PaintOptions& options)
{
    if (!canvas) {
        return;
    }

    canvas->save();
    canvas->translate(options.origin.fX - viewModel.scrollOffset().fX,
                      options.origin.fY - viewModel.scrollOffset().fY);

    // 1. Draw Selection Rectangles
    if (!viewModel.selection().is_collapsed()) {
        std::vector<SkRect> selRects;
        viewModel.document().spatial_index().getSelectionRects(
            viewModel.selection().text_range(), selRects);

        SkPaint selPaint;
        selPaint.setColor4f(options.selection_color);
        selPaint.setStyle(SkPaint::kFill_Style);

        for (const auto& r : selRects) {
            canvas->drawRect(r, selPaint);
        }
    }

    // 2. Draw Text Glyphs via Zero-Allocation Streaming Visitor
    SkRect localClip = canvas->getLocalClipBounds();
    viewModel.document().visitDocumentRuns(localClip, [&](const RenderRun& run) {
        if (run.glyphs.empty()) {
            return;
        }
        SkPaint textPaint;
        textPaint.setColor4f(run.color);
        textPaint.setAntiAlias(true);

        canvas->drawGlyphs(
            run.glyphs.size(),
            run.glyphs.data(),
            run.positions.data(),
            SkPoint::Make(0, 0),
            run.font,
            textPaint);
    });

    // 3. Draw Caret
    if (options.show_caret && viewModel.isCaretVisible()) {
        const auto& focus = viewModel.selection().focus;
        SkRect caretRect = focus.caret_rect;
        if (caretRect.isEmpty() || caretRect.height() <= 0) {
            const auto& lines = viewModel.document().formatted().lines();
            if (!lines.empty()) {
                const auto& firstLine = lines[0];
                caretRect = SkRect::MakeXYWH(firstLine.bounds.fLeft, firstLine.baseline + firstLine.ascent,
                                             options.caret_width, std::abs(firstLine.ascent) + std::abs(firstLine.descent));
            } else {
                caretRect = SkRect::MakeXYWH(0, 0, options.caret_width, 16.0f);
            }
        } else {
            caretRect.fRight = caretRect.fLeft + options.caret_width;
        }

        SkPaint caretPaint;
        caretPaint.setColor4f(options.caret_color);
        caretPaint.setStyle(SkPaint::kFill_Style);
        canvas->drawRect(caretRect, caretPaint);
    }

    canvas->restore();
}

} // namespace skia::text_editor
