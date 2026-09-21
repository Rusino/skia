/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef TextDocument_DEFINED
#define TextDocument_DEFINED

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkRect.h"
#include "include/core/SkSpan.h"
#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/FormattedParagraph.h"
#include "tools/text_editor/include/ParagraphSpatialIndex.h"
#include "tools/text_editor/include/ShapedParagraph.h"
#include "tools/text_editor/include/UnicodeParagraph.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace skia::text_editor {

/**
 * TextDocument (Model in MVVM):
 * Concrete domain state container owning the raw text, styles, and the immutable 4-layer
 * typography pipeline (UnicodeParagraph -> ShapedParagraph -> FormattedParagraph -> ParagraphSpatialIndex).
 *
 * Guaranteed Invariants:
 * 1. Monotonic Revision Invariant: Every mutating operation strictly increments revision().
 * 2. Pipeline Integrity Invariant: Layers 1-4 are guaranteed to be fully consistent
 *    with current text, styles, and constraints at all times.
 * 3. Style Span Range Invariant: Any text mutation (insert, erase, replace) automatically
 *    shifts and truncates overlapping/subsequent StyleSpans to preserve style continuity.
 * 4. Zero View Leaks: TextDocument contains zero knowledge of carets, selections, scroll offsets,
 *    blinking timers, or windowing/rendering constructs.
 */
class TextDocument {
public:
    TextDocument(std::string initial_text,
                 SkFont default_font,
                 SkColor4f text_color = SkColor4f{0, 0, 0, 1},
                 LayoutConstraints constraints = LayoutConstraints{});

    TextDocument(std::string initial_text,
                 std::vector<StyleSpan> styles,
                 LayoutConstraints constraints = LayoutConstraints{});

    ~TextDocument() = default;

    // Document State Queries (Fully Inlined)
    std::string_view text() const { return fText; }
    SkSpan<const StyleSpan> styles() const { return fStyles; }
    const LayoutConstraints& constraints() const { return fConstraints; }
    uint64_t revision() const { return fRevision; }

    // Direct Access to the 4 Immutable Layout Layers
    const ParagraphSpatialIndex& spatial_index() const { return *fSpatialIndex; }
    const FormattedParagraph& formatted() const { return fSpatialIndex->formatted(); }
    const ShapedParagraph& shaped() const { return formatted().shaped(); }
    const UnicodeParagraph& unicode() const { return shaped().unicode(); }

    // Streaming Visitor for Paint Layer (Document coordinates)
    void visitDocumentRuns(const SkRect& docClip, RenderRunVisitor visitor) const;

    // Domain Mutations (Rebuilds Layers 1-4, shifts styles, and increments revision)
    void insert(TextIndex pos, std::string_view utf8_text);
    void erase(TextRange range);
    void replace(TextRange range, std::string_view utf8_text);
    void setStyles(std::vector<StyleSpan> styles);
    void setConstraints(LayoutConstraints constraints);

private:
    void rebuildPipeline();
    void shiftStylesOnInsert(size_t pos, size_t len);
    void shiftStylesOnErase(size_t start, size_t len);

    std::string fText;
    std::vector<StyleSpan> fStyles;
    LayoutConstraints fConstraints;
    uint64_t fRevision{0};
    std::unique_ptr<const ParagraphSpatialIndex> fSpatialIndex;
};

static_assert(!std::is_aggregate_v<TextDocument>,
    "KEEPER: Domain entity must be strictly encapsulated; raw fields are prohibited");

} // namespace skia::text_editor

#endif // TextDocument_DEFINED
