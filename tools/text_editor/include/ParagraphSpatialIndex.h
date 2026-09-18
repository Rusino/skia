/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ParagraphSpatialIndex_DEFINED
#define ParagraphSpatialIndex_DEFINED

#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/FormattedParagraph.h"
#include <memory>
#include <vector>

namespace skia::text_editor {

/**
 * ClusterBox represents a precomputed visual cluster on a specific line.
 * Owned directly by LineBox in visual order. Moving left/right is index-1/index+1.
 */
struct ClusterBox {
    ClusterIndex cluster_index{0};
    TextRange text_range;
    GlyphRange glyph_range;
    SkRect bounds{SkRect::MakeEmpty()}; // Bounding box in paragraph coordinates
    bool is_rtl{false};
};

/**
 * Layer 4: Querying & Spatial Index Layer
 * Zero-heap, lean spatial query engine for cursor navigation and hit-testing.
 */
class ParagraphSpatialIndex {
public:
    virtual ~ParagraphSpatialIndex() = default;

    virtual const FormattedParagraph& formatted() const = 0;

    // Zero-heap hot-path hit-testing
    virtual CaretPosition hitTest(SkScalar x, SkScalar y) const = 0;

    // Unified cursor movement:
    // Takes direction (Left, Right, Up, Down), step granularity, and navigation mode
    // (kScreenPhysical for screen-relative arrows, kTextLogical for reading/buffer order).
    virtual CaretPosition moveCaret(
        const CaretPosition& current,
        CursorDirection dir,
        MovementGranularity granularity,
        NavigationMode mode = NavigationMode::kScreenPhysical) const = 0;

    // Word boundary lookup (for double-click word selection)
    virtual TextRange getWordBoundary(TextIndex codepoint_index) const = 0;

    // Geometry queries
    virtual void getSelectionRects(TextRange range, std::vector<SkRect>& out_rects) const = 0;

    // Factory method
    static std::unique_ptr<const ParagraphSpatialIndex> Make(
        std::shared_ptr<const FormattedParagraph> formatted_para);
};

} // namespace skia::text_editor

#endif // ParagraphSpatialIndex_DEFINED
