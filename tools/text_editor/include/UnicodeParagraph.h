/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef UnicodeParagraph_DEFINED
#define UnicodeParagraph_DEFINED

#include "tools/text_editor/include/EditorTypes.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace skia::text_editor {

enum class ClusterType : uint8_t {
    kStandardText,
    kEmoji,
    kSymbolLigature,
};

struct ItemizedRun {
    TextRange text_range;
    SkFont font;
    uint8_t bidi_level{0};
    Direction direction{Direction::kLTR};
    uint32_t script{0}; // ISO 15924 / hb_script_t
    ClusterType cluster_type{ClusterType::kStandardText};
};

struct LineBreakOpportunity {
    TextIndex offset{0};
    bool is_hard_break{false}; // True if '\n', '\r', etc.; false if soft wrap opportunity
};

/**
 * Layer 1: Unicode & Itemization Layer
 * Pure immutable container representing an itemized paragraph.
 */
class UnicodeParagraph {
public:
    virtual ~UnicodeParagraph() = default;

    virtual std::string_view text() const = 0;
    virtual SkSpan<const ItemizedRun> runs() const = 0;

    // Segmentation boundaries (UAX #29 & UAX #14)
    virtual SkSpan<const TextIndex> grapheme_breaks() const = 0;
    virtual SkSpan<const TextIndex> word_breaks() const = 0;
    virtual SkSpan<const LineBreakOpportunity> line_breaks() const = 0;

    // Per-codepoint character property queries (aligned with SkUnicode)
    virtual bool isWhitespace(TextIndex offset) const = 0;
    virtual bool isSpace(TextIndex offset) const = 0;          // Space participating in word breaks / justification
    virtual bool isTabulation(TextIndex offset) const = 0;     // '\t' requiring dynamic tab-stop advance
    virtual bool isHardLineBreak(TextIndex offset) const = 0;  // '\n', '\r', '\u2028', '\u2029', CRLF
    virtual bool isControl(TextIndex offset) const = 0;        // C0/C1, BiDi overrides, invisible formatters

    // BiDi visual reshuffling (UAX #9 Rule L2)
    virtual void reorderVisual(
        SkSpan<const uint8_t> run_levels,
        SkSpan<int32_t> out_visual_to_logical) const = 0;

    // Factory method
    static std::unique_ptr<const UnicodeParagraph> Make(
        std::string_view utf8_text,
        SkSpan<const StyleSpan> styles);
};

} // namespace skia::text_editor

#endif // UnicodeParagraph_DEFINED
