/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef EditorTypes_DEFINED
#define EditorTypes_DEFINED

#include "include/core/SkFont.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"
#include "include/core/SkSpan.h"
#include "include/core/SkTypes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

namespace skia::text_editor {

// --- Strong Type Index Wrappers (Zero Overhead, Zero Implicit Mixing) ---

template <typename Tag>
struct StrongIndex {
    size_t value{0};

    constexpr StrongIndex() = default;
    constexpr explicit StrongIndex(size_t v) : value(v) {}

    constexpr explicit operator size_t() const { return value; }
    constexpr bool operator==(const StrongIndex& other) const { return value == other.value; }
    constexpr bool operator!=(const StrongIndex& other) const { return value != other.value; }
    constexpr bool operator<(const StrongIndex& other) const { return value < other.value; }
    constexpr bool operator<=(const StrongIndex& other) const { return value <= other.value; }
    constexpr bool operator>(const StrongIndex& other) const { return value > other.value; }
    constexpr bool operator>=(const StrongIndex& other) const { return value >= other.value; }

    constexpr StrongIndex operator+(size_t offset) const { return StrongIndex(value + offset); }
    constexpr StrongIndex operator-(size_t offset) const { return StrongIndex(value - offset); }
    constexpr size_t operator-(const StrongIndex& other) const { return value - other.value; }

    constexpr StrongIndex& operator++() { ++value; return *this; }
    constexpr StrongIndex operator++(int) { StrongIndex tmp = *this; ++value; return tmp; }
    constexpr StrongIndex& operator--() { --value; return *this; }
    constexpr StrongIndex operator--(int) { StrongIndex tmp = *this; --value; return tmp; }
};

struct TextIndexTag {};
struct GlyphIndexTag {};
struct ClusterIndexTag {};

using TextIndex = StrongIndex<TextIndexTag>;       // UTF-8 byte offset
using GlyphIndex = StrongIndex<GlyphIndexTag>;     // Index in shaped glyph array
using ClusterIndex = StrongIndex<ClusterIndexTag>; // Grapheme / shaping cluster index

template <typename IndexType>
struct StrongRange {
    IndexType start{0};
    IndexType end{0};

    constexpr StrongRange() = default;
    constexpr StrongRange(IndexType s, IndexType e) : start(s), end(e) {}
    constexpr StrongRange(size_t s, size_t e) : start(IndexType(s)), end(IndexType(e)) {}

    constexpr bool operator==(const StrongRange& other) const {
        return start == other.start && end == other.end;
    }
    constexpr bool operator!=(const StrongRange& other) const {
        return !(*this == other);
    }
    constexpr bool empty() const { return start >= end; }
    constexpr size_t length() const { return end > start ? (size_t)(end - start) : 0; }
    constexpr bool contains(IndexType index) const { return index >= start && index < end; }
};

using TextRange = StrongRange<TextIndex>;
using GlyphRange = StrongRange<GlyphIndex>;
using ClusterRange = StrongRange<ClusterIndex>;

// --- Typography & Navigation Enums ---

enum class TextAlign : uint8_t {
    kLeft,
    kRight,
    kCenter,
    kJustify,
    kStart,
    kEnd,
};

enum class Direction : uint8_t {
    kLTR,
    kRTL,
};

enum class CursorDirection : uint8_t {
    kLeft,
    kRight,
    kUp,
    kDown,
};

enum class NavigationMode : uint8_t {
    kScreenPhysical, // Moves physically on screen (e.g. Right = physical right, Left = physical left)
    kTextLogical,    // Moves along buffer memory order (+1 = next codepoint/reading direction)
};

enum class MovementGranularity : uint8_t {
    kGrapheme,   // Normal character / cluster stepping
    kWord,       // Word-by-word (Ctrl/Option + Arrow)
    kLine,       // Visual line start / end (Home/End or Cmd+Arrow)
    kParagraph,  // Paragraph start / end
};

enum class Affinity : uint8_t {
    kUpstream,   // Associated with the preceding character
    kDownstream, // Associated with the following character
};

struct StyleSpan {
    TextRange range;
    SkFont font;
    SkColor4f color{0, 0, 0, 1};
};

class CaretPosition {
public:
    CaretPosition() = default;
    CaretPosition(TextIndex idx, Affinity aff = Affinity::kDownstream, SkRect rect = SkRect::MakeEmpty())
        : fTextIndex(idx), fAffinity(aff), fCaretRect(rect) {}

    TextIndex text_index() const { return fTextIndex; }
    Affinity affinity() const { return fAffinity; }
    const SkRect& caret_rect() const { return fCaretRect; }

    bool operator==(const CaretPosition& other) const {
        return fTextIndex == other.fTextIndex &&
               fAffinity == other.fAffinity &&
               fCaretRect == other.fCaretRect;
    }
    bool operator!=(const CaretPosition& other) const {
        return !(*this == other);
    }

private:
    TextIndex fTextIndex{0};
    Affinity fAffinity{Affinity::kDownstream};
    SkRect fCaretRect{SkRect::MakeEmpty()};
};

static_assert(!std::is_aggregate_v<CaretPosition>,
    "KEEPER: Domain entity must be strictly encapsulated; raw fields are prohibited");

class EditorSelection {
public:
    EditorSelection() = default;
    EditorSelection(CaretPosition a, CaretPosition f) : fAnchor(a), fFocus(f) {}
    EditorSelection(CaretPosition a, CaretPosition f, std::vector<TextRange> r)
        : fAnchor(a), fFocus(f), fRanges(std::move(r)) {}

    // Read-only accessors enforcing const-correctness (Axiom 14 / Domain Invariant 20)
    const CaretPosition& anchor() const { return fAnchor; }
    const CaretPosition& focus() const { return fFocus; }
    const std::vector<TextRange>& ranges() const { return fRanges; }

    bool is_collapsed() const {
        if (!fRanges.empty()) {
            return false;
        }
        return fAnchor.text_index() == fFocus.text_index() && fAnchor.affinity() == fFocus.affinity();
    }

    TextRange text_range() const {
        if (!fRanges.empty()) {
            return TextRange(fRanges.front().start, fRanges.back().end);
        }
        TextIndex s = std::min(fAnchor.text_index(), fFocus.text_index());
        TextIndex e = std::max(fAnchor.text_index(), fFocus.text_index());
        return TextRange(s, e);
    }

    // Cohesive State Invariant: Atomic mutators preventing broken or inconsistent ranges
    void collapse_to(CaretPosition pos) {
        fAnchor = pos;
        fFocus = pos;
        fRanges.clear();
        SkASSERT(fRanges.empty());
        SkASSERT(fAnchor == fFocus);
        SkASSERT(is_collapsed());
    }

    void set_span(CaretPosition a, CaretPosition f) {
        fAnchor = a;
        fFocus = f;
        fRanges.clear();
        SkASSERT(fRanges.empty());
    }

    void set_ranges(CaretPosition a, CaretPosition f, std::vector<TextRange> r) {
        fAnchor = a;
        fFocus = f;
        fRanges = std::move(r);
        SkASSERT(!fRanges.empty() || fAnchor == fFocus);
    }

    bool operator==(const EditorSelection& other) const {
        return fAnchor == other.fAnchor && fFocus == other.fFocus && fRanges == other.fRanges;
    }
    bool operator!=(const EditorSelection& other) const {
        return !(*this == other);
    }

private:
    CaretPosition fAnchor;
    CaretPosition fFocus;
    // For cross-directional BiDi selections, contains the exact discontinuous logical ranges
    std::vector<TextRange> fRanges;
};

// Axiom 16: Compile-time aggregate rejection.
// Ensures EditorSelection cannot be used as an anemic aggregate struct with exposed raw fields.
static_assert(!std::is_aggregate_v<EditorSelection>,
    "KEEPER-INVARIANT-20-BREACH: EditorSelection must not be an aggregate struct; fields must be private.");

} // namespace skia::text_editor

#endif // EditorTypes_DEFINED
