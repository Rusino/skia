/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef FormattedParagraph_DEFINED
#define FormattedParagraph_DEFINED

#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/ShapedParagraph.h"
#include <memory>
#include <vector>

namespace skia::text_editor {

/**
 * VisualRun represents a self-contained piece of a shaped run placed onto a specific line.
 * All glyph positions and x_offsets within VisualRun are pre-shifted to their final absolute
 * line coordinates (including centering, right alignment, or justification).
 */
struct VisualRun {
    SkFont font;
    SkColor4f color{0, 0, 0, 1};
    TextRange text_range;
    std::vector<ShapedGlyph> glyphs; // Holds shaped glyphs (including any terminal ellipsis glyph)
    SkScalar x_offset{0};            // Absolute X coordinate within paragraph (alignment baked in)
    SkScalar width{0};
    SkScalar ascent{0};
    SkScalar descent{0};
    uint8_t bidi_level{0};
    bool is_ellipsis_terminated{false};

    bool isRTL() const { return (bidi_level % 2) != 0; }
};

struct LineBox {
    size_t line_index{0};
    TextRange text_range;
    TextRange trimmed_text_range; // Excludes trailing hanging whitespace and hard breaks
    SkRect bounds{SkRect::MakeEmpty()}; // Dynamic visual bounds in paragraph coordinates (expands for Zalgo)
    SkScalar baseline{0};         // Absolute Y baseline within paragraph
    SkScalar ascent{0};
    SkScalar descent{0};
    SkScalar content_width{0};    // Visual width excluding trailing hanging spaces
    SkScalar total_width{0};      // Total width including hanging spaces
    bool has_hard_break{false};   // True if terminated by \n, \r, CRLF, etc.
    bool has_ellipsis{false};     // True if line was truncated by max_lines or width constraint
    std::vector<VisualRun> visual_runs; // Reordered visual runs (BiDi) with final baked X coordinates
};

struct LayoutConstraints {
    SkScalar max_width{SK_ScalarInfinity};
    size_t max_lines{0};          // 0 = unlimited
    std::string ellipsis;         // e.g. "\u2026"
    TextAlign align{TextAlign::kLeft};
};

/**
 * Layer 3: Formatting Layer (Line Breaking & Layout)
 * Immutable multi-line layout with dynamic Zalgo ascent/descent expansion.
 */
class FormattedParagraph {
public:
    virtual ~FormattedParagraph() = default;

    virtual const ShapedParagraph& shaped() const = 0;
    virtual SkSpan<const LineBox> lines() const = 0;
    virtual SkScalar width() const = 0;
    virtual SkScalar height() const = 0;

    // Factory method
    static std::unique_ptr<const FormattedParagraph> Make(
        std::shared_ptr<const ShapedParagraph> shaped_para,
        const LayoutConstraints& constraints);
};

} // namespace skia::text_editor

#endif // FormattedParagraph_DEFINED
