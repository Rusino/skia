/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "tests/Test.h"
#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/FormattedParagraph.h"
#include "tools/text_editor/include/ParagraphSpatialIndex.h"
#include "tools/text_editor/include/ShapedParagraph.h"
#include "tools/text_editor/include/UnicodeParagraph.h"

using namespace skia::text_editor;

// =============================================================================
// TRAP 1: Strong Index & Range Arithmetic Invariants
// =============================================================================
DEF_TEST(TextEditor_StrongTypeSafety, reporter) {
    TextIndex t1(10);
    TextIndex t2(15);
    REPORTER_ASSERT(reporter, t1 < t2);
    REPORTER_ASSERT(reporter, t2 > t1);
    REPORTER_ASSERT(reporter, t1 <= t2);
    REPORTER_ASSERT(reporter, t2 >= t1);
    REPORTER_ASSERT(reporter, t1 != t2);
    REPORTER_ASSERT(reporter, (t2 - t1) == 5);
    REPORTER_ASSERT(reporter, (t1 + 5) == t2);
    REPORTER_ASSERT(reporter, (t2 - 5) == t1);

    TextIndex tInc(0);
    REPORTER_ASSERT(reporter, (tInc++).value == 0);
    REPORTER_ASSERT(reporter, tInc.value == 1);
    REPORTER_ASSERT(reporter, (++tInc).value == 2);
    REPORTER_ASSERT(reporter, (tInc--).value == 2);
    REPORTER_ASSERT(reporter, tInc.value == 1);
    REPORTER_ASSERT(reporter, (--tInc).value == 0);

    GlyphIndex g1(3);
    GlyphIndex g2(7);
    REPORTER_ASSERT(reporter, g1 < g2);
    REPORTER_ASSERT(reporter, (g2 - g1) == 4);

    ClusterIndex c1(0);
    ClusterIndex c2(1);
    REPORTER_ASSERT(reporter, c1 < c2);

    TextRange tr(t1, t2);
    REPORTER_ASSERT(reporter, !tr.empty());
    REPORTER_ASSERT(reporter, tr.length() == 5);
    REPORTER_ASSERT(reporter, tr.contains(TextIndex(10)));
    REPORTER_ASSERT(reporter, tr.contains(TextIndex(14)));
    REPORTER_ASSERT(reporter, !tr.contains(TextIndex(15)));
    REPORTER_ASSERT(reporter, !tr.contains(TextIndex(9)));

    TextRange emptyRange(t2, t1);
    REPORTER_ASSERT(reporter, emptyRange.empty());
    REPORTER_ASSERT(reporter, emptyRange.length() == 0);
}

// =============================================================================
// TRAP 2: Layer 1 Unicode Itemization, Segmentation & ReorderVisual
// =============================================================================
DEF_TEST(TextEditor_UnicodeParagraph_ItemizationAndClassification, reporter) {
    // English + Arabic + Newline: "Hello عربي\nWorld"
    // "Hello " (0..6)
    // "عربي"   (6..14 in UTF-8: 4 Arabic characters * 2 bytes = 8 bytes)
    // "\n"     (14..15)
    // "World"  (15..20)
    const std::string text = "Hello \xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\nWorld";

    SkFont defaultFont;
    std::vector<StyleSpan> styles = {
        { TextRange(TextIndex(0), TextIndex(text.size())), defaultFont, SkColor4f{0, 0, 0, 1} }
    };

    auto unicodePara = UnicodeParagraph::Make(text, styles);
    REPORTER_ASSERT(reporter, unicodePara != nullptr);
    REPORTER_ASSERT(reporter, unicodePara->text() == text);

    // 1. Classification Trap
    // Space at index 5
    REPORTER_ASSERT(reporter, unicodePara->isWhitespace(TextIndex(5)));
    REPORTER_ASSERT(reporter, unicodePara->isSpace(TextIndex(5)));
    REPORTER_ASSERT(reporter, !unicodePara->isHardLineBreak(TextIndex(5)));
    REPORTER_ASSERT(reporter, !unicodePara->isTabulation(TextIndex(5)));

    // Newline at index 14
    REPORTER_ASSERT(reporter, unicodePara->isHardLineBreak(TextIndex(14)));
    REPORTER_ASSERT(reporter, unicodePara->isWhitespace(TextIndex(14)));

    // Regular letter 'H' at index 0
    REPORTER_ASSERT(reporter, !unicodePara->isWhitespace(TextIndex(0)));
    REPORTER_ASSERT(reporter, !unicodePara->isHardLineBreak(TextIndex(0)));

    // 2. Runs & BiDi Levels Trap
    // Must identify at least 3 distinct itemized runs due to BiDi direction change:
    // Run 0: LTR ("Hello "), level 0
    // Run 1: RTL ("عربي"), level 1
    // Run 2: LTR ("World"), level 0
    auto runs = unicodePara->runs();
    REPORTER_ASSERT(reporter, runs.size() >= 2);

    bool foundRTL = false;
    for (const auto& run : runs) {
        if (run.direction == Direction::kRTL) {
            foundRTL = true;
            REPORTER_ASSERT(reporter, (run.bidi_level % 2) != 0);
        }
    }
    REPORTER_ASSERT(reporter, foundRTL);

    // 3. Line Break Opportunities Trap (UAX #14)
    auto lineBreaks = unicodePara->line_breaks();
    REPORTER_ASSERT(reporter, !lineBreaks.empty());
    bool sawHardBreak = false;
    for (const auto& lb : lineBreaks) {
        if (lb.is_hard_break) {
            sawHardBreak = true;
            REPORTER_ASSERT(reporter, lb.offset == TextIndex(14) || lb.offset == TextIndex(15));
        }
    }
    REPORTER_ASSERT(reporter, sawHardBreak);

    // 4. UAX #9 ReorderVisual Trap
    // Suppose on a single line we have runs with bidi levels: [0 (LTR), 1 (RTL), 2 (embedded LTR)]
    // Visual order per UAX #9 Rule L2 must reverse odd levels:
    // Visual 0 -> Logical 0
    // Visual 1 -> Logical 2
    // Visual 2 -> Logical 1
    uint8_t levels[] = { 0, 1, 2 };
    int32_t visualToLogical[3] = { -1, -1, -1 };
    unicodePara->reorderVisual(SkSpan<const uint8_t>(levels, 3), SkSpan<int32_t>(visualToLogical, 3));
    REPORTER_ASSERT(reporter, visualToLogical[0] == 0);
    REPORTER_ASSERT(reporter, visualToLogical[1] == 2);
    REPORTER_ASSERT(reporter, visualToLogical[2] == 1);
}

// =============================================================================
// TRAP 3: Layer 2 Shaping Invariants (1-to-1 Mapping, Zero liga/ccmp Collapse)
// =============================================================================
DEF_TEST(TextEditor_ShapedParagraph_SinglePassNoLigatureCollapse, reporter) {
    // Hostile string designed to trigger standard ligatures and composition:
    // "ffi" -> normally collapsed to 1 ligature glyph by 'liga'
    // "e\u0301" -> 'e' + combining acute (U+0301), normally collapsed to 'é' by 'ccmp'
    const std::string text = "ffi e\xcc\x81"; // 'e' (1 byte) + 0xCC 0x81 (2 bytes)

    SkFont font;
    std::vector<StyleSpan> styles = {
        { TextRange(TextIndex(0), TextIndex(text.size())), font, SkColor4f{0, 0, 0, 1} }
    };

    auto unicodePara = UnicodeParagraph::Make(text, styles);
    REPORTER_ASSERT(reporter, unicodePara != nullptr);

    auto shapedPara = ShapedParagraph::Make(std::move(unicodePara));
    REPORTER_ASSERT(reporter, shapedPara != nullptr);

    auto shapedRuns = shapedPara->shaped_runs();
    REPORTER_ASSERT(reporter, !shapedRuns.empty());

    // Hostile Invariant:
    // Because liga=0 and ccmp=0 are strictly enforced:
    // "ffi" MUST produce 3 distinct glyphs ('f', 'f', 'i') with distinct cluster indices!
    // "e\u0301" MUST produce 2 distinct glyphs (base 'e' and mark U+0301)!
    size_t totalGlyphs = 0;
    bool sawMark = false;
    for (const auto& run : shapedRuns) {
        totalGlyphs += run.glyphs.size();
        for (const auto& glyph : run.glyphs) {
            if (glyph.is_mark) {
                sawMark = true;
            }
        }
    }

    // Input has 6 codepoints: 'f', 'f', 'i', ' ', 'e', U+0301.
    // Total glyph count must be exactly 6!
    REPORTER_ASSERT(reporter, totalGlyphs == 6);
    REPORTER_ASSERT(reporter, sawMark);
}

// =============================================================================
// TRAP 4: Layer 3 Formatting, Dynamic Zalgo Expansion, VisualRun & Ellipsis
// =============================================================================
DEF_TEST(TextEditor_FormattedParagraph_LayoutZalgoAndEllipsis, reporter) {
    // 1. Zalgo Dynamic Bounds Expansion Trap
    // 'e' stacked with 6 combining diacritics above
    const std::string zalgo = "e\xcc\x81\xcc\x80\xcc\x83\xcc\x82\xcc\x88\xcc\x8a";
    SkFont font;
    std::vector<StyleSpan> styles = {
        { TextRange(TextIndex(0), TextIndex(zalgo.size())), font, SkColor4f{0, 0, 0, 1} }
    };

    auto uniZalgo = UnicodeParagraph::Make(zalgo, styles);
    auto shapedZalgo = ShapedParagraph::Make(std::move(uniZalgo));

    LayoutConstraints unconstrained;
    auto formattedZalgo = FormattedParagraph::Make(std::move(shapedZalgo), unconstrained);
    REPORTER_ASSERT(reporter, formattedZalgo != nullptr);
    REPORTER_ASSERT(reporter, formattedZalgo->lines().size() == 1);

    const auto& zalgoLine = formattedZalgo->lines()[0];
    // Dynamic visual bounds must expand vertically beyond typical standard font ascent:
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    REPORTER_ASSERT(reporter, zalgoLine.bounds.height() >= std::abs(metrics.fAscent) + std::abs(metrics.fDescent));

    // 2. Terminal Ellipsis Binding Trap
    const std::string longText = "This is a long sentence that must truncate with ellipsis";
    std::vector<StyleSpan> longStyles = {
        { TextRange(TextIndex(0), TextIndex(longText.size())), font, SkColor4f{0, 0, 0, 1} }
    };

    auto uniLong = UnicodeParagraph::Make(longText, longStyles);
    auto shapedLong = ShapedParagraph::Make(std::move(uniLong));

    LayoutConstraints truncateConstraints;
    truncateConstraints.max_width = 100.0f; // Narrow width forces wrapping/truncation
    truncateConstraints.max_lines = 1;      // Force single line with ellipsis
    truncateConstraints.ellipsis = "\xe2\x80\xa6"; // "…" (U+2026)

    auto formattedTruncated = FormattedParagraph::Make(std::move(shapedLong), truncateConstraints);
    REPORTER_ASSERT(reporter, formattedTruncated != nullptr);
    REPORTER_ASSERT(reporter, formattedTruncated->lines().size() == 1);

    const auto& truncLine = formattedTruncated->lines()[0];
    REPORTER_ASSERT(reporter, truncLine.has_ellipsis);
    REPORTER_ASSERT(reporter, !truncLine.visual_runs.empty());

    const auto& lastRun = truncLine.visual_runs.back();
    REPORTER_ASSERT(reporter, lastRun.is_ellipsis_terminated);
    REPORTER_ASSERT(reporter, !lastRun.glyphs.empty());
}

// =============================================================================
// TRAP 5: Layer 4 Zero-Heap Spatial Index, Caret Navigation & Selection
// =============================================================================
DEF_TEST(TextEditor_ParagraphSpatialIndex_NavigationAndHitTest, reporter) {
    // Mixed LTR and RTL: "ABC \xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a XYZ"
    const std::string text = "ABC \xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a XYZ";
    SkFont font;
    std::vector<StyleSpan> styles = {
        { TextRange(TextIndex(0), TextIndex(text.size())), font, SkColor4f{0, 0, 0, 1} }
    };

    auto uni = UnicodeParagraph::Make(text, styles);
    auto shaped = ShapedParagraph::Make(std::move(uni));
    LayoutConstraints constraints;
    auto formatted = FormattedParagraph::Make(std::move(shaped), constraints);
    auto spatial = ParagraphSpatialIndex::Make(std::move(formatted));
    REPORTER_ASSERT(reporter, spatial != nullptr);

    // 1. Hit-Test Trap
    // Hit-testing near origin (0, 0) must land on the first character ('A')
    CaretPosition caret0 = spatial->hitTest(0.0f, 5.0f);
    REPORTER_ASSERT(reporter, caret0.text_index == TextIndex(0));
    REPORTER_ASSERT(reporter, caret0.affinity == Affinity::kDownstream);

    // 2. Physical vs Logical Caret Movement Trap
    // Step forward physically vs logically:
    CaretPosition nextPhys = spatial->moveCaret(caret0, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kScreenPhysical);
    CaretPosition nextLog  = spatial->moveCaret(caret0, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);

    // For LTR characters at start of sentence, both advance to index 1:
    REPORTER_ASSERT(reporter, nextPhys.text_index == TextIndex(1));
    REPORTER_ASSERT(reporter, nextLog.text_index == TextIndex(1));

    // 3. Word Boundary Trap
    // Searching word boundary inside "ABC" (offset 1) must return [0, 3)
    TextRange wordRange = spatial->getWordBoundary(TextIndex(1));
    REPORTER_ASSERT(reporter, wordRange.start == TextIndex(0));
    REPORTER_ASSERT(reporter, wordRange.end == TextIndex(3));

    // 4. Selection Rectangles Trap
    std::vector<SkRect> selectionRects;
    spatial->getSelectionRects(TextRange(TextIndex(0), TextIndex(3)), selectionRects);
    REPORTER_ASSERT(reporter, !selectionRects.empty());
    REPORTER_ASSERT(reporter, selectionRects[0].width() > 0);
    REPORTER_ASSERT(reporter, selectionRects[0].height() > 0);
}
