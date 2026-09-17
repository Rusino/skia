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
    const TextEditorController& editor,
    const PaintOptions& options)
{
    if (!canvas) {
        return;
    }

    canvas->save();
    canvas->translate(options.origin.fX, options.origin.fY);

    const auto& spatial = editor.spatial_index();
    const auto& formatted = spatial.formatted();

    // 1. Draw Selection Rectangles
    if (!editor.selection().is_collapsed()) {
        std::vector<SkRect> selRects;
        spatial.getSelectionRects(editor.selection().text_range(), selRects);

        SkPaint selPaint;
        selPaint.setColor4f(options.selection_color);
        selPaint.setStyle(SkPaint::kFill_Style);

        for (const auto& r : selRects) {
            canvas->drawRect(r, selPaint);
        }
    }

    // 2. Draw Text Glyphs
    SkPaint textPaint;
    textPaint.setColor4f(SkColor4f{0, 0, 0, 1});
    textPaint.setAntiAlias(true);

    for (const auto& line : formatted.lines()) {
        for (const auto& vr : line.visual_runs) {
            if (vr.glyphs.empty()) {
                continue;
            }

            std::vector<SkGlyphID> glyphIds;
            std::vector<SkPoint> positions;
            glyphIds.reserve(vr.glyphs.size());
            positions.reserve(vr.glyphs.size());

            SkScalar curX = vr.x_offset;
            for (const auto& g : vr.glyphs) {
                if (g.is_zero_width_control) {
                    continue;
                }
                glyphIds.push_back(static_cast<SkGlyphID>(g.glyph_id));
                positions.push_back(SkPoint::Make(curX + g.offset.fX, line.baseline + g.offset.fY));
                curX += g.advance.fX;
            }

            canvas->drawGlyphs(
                glyphIds.size(),
                glyphIds.data(),
                positions.data(),
                SkPoint::Make(0, 0),
                vr.font,
                textPaint);
        }
    }

    // 3. Draw Caret
    if (options.show_caret) {
        const auto& focus = editor.selection().focus;
        SkRect caretRect = focus.caret_rect;
        if (caretRect.isEmpty() || caretRect.height() <= 0) {
            if (!formatted.lines().empty()) {
                const auto& firstLine = formatted.lines()[0];
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
