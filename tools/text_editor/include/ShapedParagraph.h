/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ShapedParagraph_DEFINED
#define ShapedParagraph_DEFINED

#include "include/core/SkPoint.h"
#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/UnicodeParagraph.h"
#include <memory>
#include <type_traits>
#include <vector>

namespace skia::text_editor {

struct ShapedGlyph {
    uint32_t glyph_id{0};
    TextIndex cluster_text_index{0}; // UTF-8 byte offset in original text
    SkPoint advance{0, 0};
    SkPoint offset{0, 0};
    bool is_mark{false};
    bool is_zero_width_control{false};
};

struct ShapedRun {
    const ItemizedRun* item{nullptr};
    std::vector<ShapedGlyph> glyphs;
    SkScalar width{0};
    SkScalar ascent{0};
    SkScalar descent{0};
    SkScalar leading{0};
};

/**
 * Layer 2: Shaping Layer (HarfBuzz Single-Pass)
 * Immutable representation of shaped runs with 1-to-1 codepoint-to-glyph mapping.
 */
class ShapedParagraph {
public:
    virtual ~ShapedParagraph() = default;

    virtual const UnicodeParagraph& unicode() const = 0;
    virtual SkSpan<const ShapedRun> shaped_runs() const = 0;
    virtual SkScalar advance_width() const = 0;

    // Factory method
    static std::unique_ptr<const ShapedParagraph> Make(
        std::shared_ptr<const UnicodeParagraph> unicode_para);
};

static_assert(!std::is_aggregate_v<ShapedParagraph>,
    "KEEPER: Domain entity must be strictly encapsulated; raw fields are prohibited");

} // namespace skia::text_editor

#endif // ShapedParagraph_DEFINED
