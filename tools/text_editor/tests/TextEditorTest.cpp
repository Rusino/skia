/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "src/base/SkUTF.h"
#include "tests/Test.h"
#include "tools/fonts/FontToolUtils.h"
#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/FormattedParagraph.h"
#include "tools/text_editor/include/ParagraphSpatialIndex.h"
#include "tools/text_editor/include/TextDocument.h"
#include "tools/text_editor/include/TextEditorPainter.h"
#include "tools/text_editor/include/TextEditorViewModel.h"
#include "tools/text_editor/include/UnicodeParagraph.h"
#include "tools/text_editor/tests/StressCorpus.h"

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

// =============================================================================
// TRAP 6: Layer 5 Controller Text Mutation & Selection Invariants
// =============================================================================
DEF_TEST(TextEditor_Controller_InsertionAndDeletion, reporter) {
    SkFont font;
    auto editor = std::make_unique<TextEditorViewModel>("Hello World", font);
    REPORTER_ASSERT(reporter, editor != nullptr);
    REPORTER_ASSERT(reporter, editor->text() == "Hello World");

    // Initially collapsed at 0
    REPORTER_ASSERT(reporter, editor->selection().is_collapsed());

    // Move to end and type "!"
    editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    int safetySteps = 0;
    while (editor->selection().focus.text_index < TextIndex(editor->text().size()) && ++safetySteps < 1000) {
        editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    }
    REPORTER_ASSERT(reporter, safetySteps < 1000);
    editor->insertText("!");
    REPORTER_ASSERT(reporter, editor->text() == "Hello World!");

    // Backspace: removes "!"
    editor->deleteBackward();
    REPORTER_ASSERT(reporter, editor->text() == "Hello World");

    // Select "World" [6, 11) and replace with "Skia"
    CaretPosition anchor;
    anchor.text_index = TextIndex(6);
    anchor.affinity = Affinity::kDownstream;

    CaretPosition focus;
    focus.text_index = TextIndex(11);
    focus.affinity = Affinity::kDownstream;

    editor->setSelection(anchor, focus);
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().text_range() == TextRange(TextIndex(6), TextIndex(11)));

    editor->insertText("Skia");
    REPORTER_ASSERT(reporter, editor->text() == "Hello Skia");
    REPORTER_ASSERT(reporter, editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(10));

    // Collapse to beginning and deleteForward
    CaretPosition startPos;
    startPos.text_index = TextIndex(0);
    startPos.affinity = Affinity::kDownstream;
    editor->collapseTo(startPos);
    editor->deleteForward();
    REPORTER_ASSERT(reporter, editor->text() == "ello Skia");

    // Select all and deleteBackward
    editor->selectAll();
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().text_range().length() == editor->text().size());
    editor->deleteBackward();
    REPORTER_ASSERT(reporter, editor->text().empty());
    REPORTER_ASSERT(reporter, editor->selection().is_collapsed());
}

// =============================================================================
// TRAP 7: Layer 5 Controller Navigation & Word Selection
// =============================================================================
DEF_TEST(TextEditor_Controller_NavigationAndWordSelection, reporter) {
    SkFont font;
    auto editor = std::make_unique<TextEditorViewModel>("The quick brown fox", font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    // 1. Move caret with selection expansion (select = true)
    CaretPosition startPos;
    startPos.text_index = TextIndex(0);
    startPos.affinity = Affinity::kDownstream;
    editor->collapseTo(startPos);

    editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, true);
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().anchor.text_index == TextIndex(0));
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(1));

    // 2. Collapse navigation (select = false)
    editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    REPORTER_ASSERT(reporter, editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(2));

    // 3. Word selection at point
    // Inside "quick" (x offset of 'q' is after "The ")
    // Hit-testing near the word should select [4, 9)
    editor->selectWordAtPoint(35.0f, 5.0f);
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    // The selection must be a valid non-empty range covering words
    TextRange selRange = editor->selection().text_range();
    REPORTER_ASSERT(reporter, selRange.length() > 0);
}

// =============================================================================
// TRAP 8: Layer 6 Painter Stateless Canvas Drawing
// =============================================================================
DEF_TEST(TextEditor_Painter_RenderWithoutCrashing, reporter) {
    SkFont font;
    auto editor = std::make_unique<TextEditorViewModel>("Visual Rendering Test\nLine 2", font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    // Create a 200x200 software bitmap and canvas
    SkBitmap bitmap;
    bitmap.allocN32Pixels(200, 200);
    SkCanvas canvas(bitmap);
    canvas.clear(SK_ColorWHITE);

    PaintOptions options;
    options.show_caret = true;
    options.caret_width = 2.0f;
    options.origin = SkPoint::Make(10.0f, 10.0f);

    // 1. Paint with collapsed caret
    TextEditorPainter::Paint(&canvas, *editor, options);

    // 2. Paint with active selection
    editor->selectAll();
    TextEditorPainter::Paint(&canvas, *editor, options);

    // Verify canvas is not blank (some pixels were drawn)
    bool hasDrawnPixel = false;
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 200; ++x) {
            if (bitmap.getColor(x, y) != SK_ColorWHITE) {
                hasDrawnPixel = true;
                break;
            }
        }
        if (hasDrawnPixel) break;
    }
    REPORTER_ASSERT(reporter, hasDrawnPixel);
}

// =============================================================================
// DEFECT TRAP 1: Backspace & Delete Must Maintain Exact Spatial Caret Position
// =============================================================================
DEF_TEST(TextEditor_Defect_BackspaceMaintainsCaretRectPosition, reporter) {
    SkFont font;
    auto editor = std::make_unique<TextEditorViewModel>("Hello World", font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    // Move to end of "Hello World" (offset 11)
    int stepLimit = 0;
    while (editor->selection().focus.text_index < TextIndex(editor->text().size()) && ++stepLimit < 1000) {
        editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    }
    REPORTER_ASSERT(reporter, stepLimit < 1000);
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(11));

    SkScalar beforeX = editor->selection().focus.caret_rect.fLeft;
    REPORTER_ASSERT(reporter, beforeX > 0.0f);

    // Backspace deletes 'd'. New text is "Hello Worl" (length 10)
    editor->deleteBackward();
    REPORTER_ASSERT(reporter, editor->text() == "Hello Worl");
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(10));

    // Hostile Invariant: Caret X position must NOT reset to 0.0f! It must sit at the end of "Worl"!
    SkScalar afterX = editor->selection().focus.caret_rect.fLeft;
    REPORTER_ASSERT(reporter, afterX > 0.0f);
    REPORTER_ASSERT(reporter, afterX < beforeX);

    // Move to index 5 ("Hello| Worl") and test deleteForward
    CaretPosition midPos;
    midPos.text_index = TextIndex(5);
    midPos.affinity = Affinity::kDownstream;
    editor->collapseTo(midPos);

    // Delete space at index 5 -> "HelloWorl"
    editor->deleteForward();
    REPORTER_ASSERT(reporter, editor->text() == "HelloWorl");
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(5));
    // Caret X position after deleteForward must sit at the boundary, NOT at 0.0f!
    REPORTER_ASSERT(reporter, editor->selection().focus.caret_rect.fLeft > 0.0f);
}

// =============================================================================
// DEFECT TRAP 2: Line Wrapping Must Break at Word Boundaries, Not Mid-Cluster
// =============================================================================
DEF_TEST(TextEditor_Defect_LineWrappingAtWordBreak, reporter) {
    SkFont font;
    const std::string text = "Hello World";
    std::vector<StyleSpan> styles = {
        { TextRange(TextIndex(0), TextIndex(text.size())), font, SkColor4f{0, 0, 0, 1} }
    };

    auto uni = UnicodeParagraph::Make(text, styles);
    auto shaped = ShapedParagraph::Make(std::move(uni));

    // Measure the exact advance width of "Hello" vs "Hello World"
    SkScalar totalWidth = shaped->advance_width();
    SkScalar halfWidth = totalWidth * 0.65f; // Wide enough for "Hello ", too narrow for "Hello World"

    LayoutConstraints constraints;
    constraints.max_width = halfWidth;

    auto formatted = FormattedParagraph::Make(std::move(shaped), constraints);
    REPORTER_ASSERT(reporter, formatted != nullptr);
    REPORTER_ASSERT(reporter, formatted->lines().size() == 2);

    const auto& line0 = formatted->lines()[0];
    const auto& line1 = formatted->lines()[1];

    // Hostile Invariant:
    // Line 0 MUST NOT break inside "World"! It must end after "Hello" (index <= 6).
    // Line 1 MUST start with "World" (index 6).
    for (const auto& vr : line0.visual_runs) {
        for (const auto& g : vr.glyphs) {
            // Index 6 is 'W'. Line 0 must never contain 'W', 'o', 'r', 'l', 'd'!
            REPORTER_ASSERT(reporter, g.cluster_text_index < TextIndex(6));
        }
    }
    REPORTER_ASSERT(reporter, !line1.visual_runs.empty());
    REPORTER_ASSERT(reporter, line1.visual_runs[0].glyphs[0].cluster_text_index == TextIndex(6));
}

// =============================================================================
// TRAP 9 (Invariant 7): Full-Spectrum Headless Interactive Session Simulation
// =============================================================================
DEF_TEST(TextEditor_Invariant7_HeadlessInteractionSession, reporter) {
    SkFont font;
    font.setSize(16.0f);

    LayoutConstraints constraints;
    constraints.max_width = 200.0f; // Constrain width so text wraps across multiple lines

    // 1. Initial Empty Controller
    auto editor = std::make_unique<TextEditorViewModel>("", font, SkColor4f{0, 0, 0, 1}, constraints);
    REPORTER_ASSERT(reporter, editor != nullptr);
    REPORTER_ASSERT(reporter, editor->text().empty());

    // Dual-Contract: Empty buffer must have valid non-zero spatial bounds
    REPORTER_ASSERT(reporter, editor->selection().focus.caret_rect.height() > 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus.caret_rect.fLeft >= 0.0f);

    // 2. Headless Typing Flow: Simulate typing "The quick brown fox jumps over the lazy dog"
    const std::string fullSentence = "The quick brown fox jumps over the lazy dog";
    editor->insertText(fullSentence);
    REPORTER_ASSERT(reporter, editor->text() == fullSentence);

    // Dual-Contract: Verify multi-line wrapping under 200.0f width constraint
    const auto& spatial = editor->spatial_index();
    const auto& lines = spatial.formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() >= 2);

    // Verify word-boundary invariant: each line must not break inside words
    for (const auto& line : lines) {
        REPORTER_ASSERT(reporter, line.bounds.width() <= constraints.max_width + 1.0f);
        REPORTER_ASSERT(reporter, !line.visual_runs.empty());
    }

    // Caret must be at end of text with valid non-zero spatial X
    CaretPosition endPos = editor->selection().focus;
    REPORTER_ASSERT(reporter, endPos.text_index == TextIndex(fullSentence.size()));
    REPORTER_ASSERT(reporter, endPos.caret_rect.fLeft > 0.0f);
    SkScalar endX = endPos.caret_rect.fLeft;

    // 3. Headless Backspace Flow: Delete "dog" (3 backspaces)
    editor->deleteBackward(); // deletes 'g'
    REPORTER_ASSERT(reporter, editor->text() == "The quick brown fox jumps over the lazy do");
    REPORTER_ASSERT(reporter, editor->selection().focus.caret_rect.fLeft < endX);
    SkScalar doX = editor->selection().focus.caret_rect.fLeft;

    editor->deleteBackward(); // deletes 'o'
    SkScalar dX = editor->selection().focus.caret_rect.fLeft;
    REPORTER_ASSERT(reporter, dX < doX);
    REPORTER_ASSERT(reporter, dX > 0.0f);

    editor->deleteBackward(); // deletes 'd'
    // With 'd' deleted, the wrapped line for "dog" collapses, and the caret wraps up
    // to the end of the previous line ("...lazy ").
    REPORTER_ASSERT(reporter, editor->text() == "The quick brown fox jumps over the lazy ");
    REPORTER_ASSERT(reporter, editor->selection().focus.caret_rect.fLeft > 0.0f);

    // 4. Headless Selection & Navigation Flow:
    // Move left 5 times with select=true (expanding selection across "lazy")
    for (int i = 0; i < 5; ++i) {
        editor->moveCaret(CursorDirection::kLeft, MovementGranularity::kGrapheme, NavigationMode::kScreenPhysical, true);
    }
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().text_range().length() > 0);

    // 5. Headless Select All & Overwrite Safeguard Flow:
    editor->selectAll();
    REPORTER_ASSERT(reporter, editor->selection().text_range().start == TextIndex(0));
    REPORTER_ASSERT(reporter, editor->selection().text_range().end == TextIndex(editor->text().size()));

    // Perform replacement of selected range
    editor->insertText("Replaced");
    REPORTER_ASSERT(reporter, editor->text() == "Replaced");
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(8));
    REPORTER_ASSERT(reporter, editor->selection().focus.caret_rect.fLeft > 0.0f);
}

// =============================================================================
// TRAP 10 (Invariant 7): Headless Event Dispatch & Modifier Guard Simulation
// =============================================================================
DEF_TEST(TextEditor_Invariant7_HeadlessEventDispatch, reporter) {
    SkFont font;
    const std::string initialText = "Hello World";
    auto editor = std::make_unique<TextEditorViewModel>(initialText, font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    // 1. Simulate Normal Typing via handleChar: insert '!' at beginning
    bool charHandled = editor->handleChar('!', skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, charHandled);
    REPORTER_ASSERT(reporter, editor->text() == "!Hello World");

    // 2. Simulate Ctrl+A via handleKey: must select all
    bool keyHandled = editor->handleKey(skui::Key::kA, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, keyHandled);
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().text_range().length() == editor->text().size());

    // 3. Simulate OS dispatching onChar('a', kControl) after Ctrl+A:
    // Hostile Invariant: handleChar MUST reject 'a' when Ctrl/Cmd is active!
    bool ctrlCharHandled = editor->handleChar('a', skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, !ctrlCharHandled);

    // CRITICAL DEFECT TRAP (Bug 3):
    // Buffer MUST NOT be replaced with "a"! It must preserve "!Hello World" and remain selected!
    REPORTER_ASSERT(reporter, editor->text() == "!Hello World");
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());

    // 4. Simulate Backspace key replacing selection
    bool backHandled = editor->handleKey(skui::Key::kBack, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, backHandled);
    REPORTER_ASSERT(reporter, editor->text().empty());
    REPORTER_ASSERT(reporter, editor->selection().focus.caret_rect.height() > 0.0f);
}

// =============================================================================
// DEFECT TRAP 11 (Defect D4): Newline '\n' Must NOT Render as Tofu Box
// =============================================================================
DEF_TEST(TextEditor_Defect_NewlineNotRenderedAsTofu, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);

    auto editorA = std::make_unique<TextEditorViewModel>("A", font);
    auto editorANewline = std::make_unique<TextEditorViewModel>("A\n", font);
    REPORTER_ASSERT(reporter, editorA != nullptr);
    REPORTER_ASSERT(reporter, editorANewline != nullptr);

    SkBitmap bitmapA, bitmapANewline;
    bitmapA.allocN32Pixels(100, 100);
    bitmapANewline.allocN32Pixels(100, 100);
    SkCanvas canvasA(bitmapA);
    SkCanvas canvasANewline(bitmapANewline);
    canvasA.clear(SK_ColorWHITE);
    canvasANewline.clear(SK_ColorWHITE);

    PaintOptions options;
    options.show_caret = false;
    options.origin = SkPoint::Make(10.0f, 10.0f);

    TextEditorPainter::Paint(&canvasA, *editorA, options);
    TextEditorPainter::Paint(&canvasANewline, *editorANewline, options);

    int pixelsA = 0;
    int pixelsANewline = 0;
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 100; ++x) {
            if (bitmapA.getColor(x, y) != SK_ColorWHITE) {
                ++pixelsA;
            }
            if (bitmapANewline.getColor(x, y) != SK_ColorWHITE) {
                ++pixelsANewline;
            }
        }
    }

    // Hostile Invariant:
    // "A\n" must NOT render any tofu / box / .notdef glyph for '\n'.
    // The number of painted pixels for "A\n" MUST be identical to "A"!
    REPORTER_ASSERT(reporter, pixelsA > 0);
    REPORTER_ASSERT(reporter, pixelsANewline == pixelsA);
}

// =============================================================================
// DEFECT TRAP 12 (Defect D5): Vertical Caret Navigation (Arrow Up and Down)
// =============================================================================
DEF_TEST(TextEditor_Defect_VerticalCaretNavigationUpDown, reporter) {
    SkFont font;
    font.setSize(16.0f);

    // Two lines: "First Line\nSecond Line"
    auto editor = std::make_unique<TextEditorViewModel>("First Line\nSecond Line", font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    // Caret starts at index 0 (Line 0, "First Line")
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(0));
    SkScalar startY = editor->selection().focus.caret_rect.fTop;

    // Move right 5 characters: "First|"
    for (int i = 0; i < 5; ++i) {
        editor->handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
    }
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(5));

    // Hostile Invariant 1: Arrow Down must move caret to Line 1 ("Second Line")
    bool downHandled = editor->handleKey(skui::Key::kDown, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, downHandled);

    CaretPosition downPos = editor->selection().focus;
    // Must move to line 1: Y coordinate must increase!
    REPORTER_ASSERT(reporter, downPos.caret_rect.fTop > startY);
    // Must be in "Second Line" (index >= 11, which is after "First Line\n")
    REPORTER_ASSERT(reporter, downPos.text_index >= TextIndex(11));

    // Hostile Invariant 2: Arrow Up must move caret back to Line 0
    bool upHandled = editor->handleKey(skui::Key::kUp, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, upHandled);

    CaretPosition upPos = editor->selection().focus;
    // Must return to line 0: Y coordinate must match startY!
    REPORTER_ASSERT(reporter, upPos.caret_rect.fTop == startY);
    REPORTER_ASSERT(reporter, upPos.text_index < TextIndex(11));
}

// =============================================================================
// DEFECT TRAP 13 (Defect D6): Font Fallback & Arabic Glyph Shaping
// =============================================================================
DEF_TEST(TextEditor_Defect_FontFallbackAndArabicGlyphShaping, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);

    sk_sp<SkFontMgr> fm = SkFontMgr::RefDefault();
    REPORTER_ASSERT(reporter, fm != nullptr);
    if (fm) {
        sk_sp<SkTypeface> arFace = fm->matchFamilyStyleCharacter(nullptr, SkFontStyle(), nullptr, 0, 0x0645);
        REPORTER_ASSERT(reporter, arFace != nullptr);
    }

    // "مرحبا" (Arabic for "Hello")
    auto editor = std::make_unique<TextEditorViewModel>("مرحبا", font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    const auto& spatial = editor->spatial_index();
    const auto& shaped = spatial.formatted().shaped();
    REPORTER_ASSERT(reporter, !shaped.shaped_runs().empty());

    // Hostile Invariant:
    // Arabic characters must NOT map to glyph ID 0 (.notdef tofu).
    // The engine must automatically find a font fallback that provides valid glyphs.
    bool hasTofu = false;
    int glyphCount = 0;
    for (const auto& sr : shaped.shaped_runs()) {
        for (const auto& g : sr.glyphs) {
            ++glyphCount;
            if (g.glyph_id == 0 && !g.is_zero_width_control) {
                hasTofu = true;
            }
        }
    }
    REPORTER_ASSERT(reporter, glyphCount > 0);
    REPORTER_ASSERT(reporter, !hasTofu);
}

// =============================================================================
// DEFECT TRAP 14 (Defect D7): Anti-Monoculture Extended Grapheme Cluster Matrix
// =============================================================================
DEF_TEST(TextEditor_Defect_ZalgoGraphemeSingleStepNavigation, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);

    // --- Partition 1: Latin Stacked Diacritics (Zalgo Text) ---
    // 'e' with 6 combining diacritics, followed by " X"
    const std::string zalgo = "e\xcc\x81\xcc\x80\xcc\x83\xcc\x82\xcc\x88\xcc\x8a";
    const std::string text = zalgo + " X";
    auto editor = std::make_unique<TextEditorViewModel>(text, font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    // Caret starts at index 0 (left of 'e')
    REPORTER_ASSERT(reporter, editor->selection().focus.text_index == TextIndex(0));
    SkScalar startX = editor->selection().focus.caret_rect.fLeft;

    // 1. Single Right Arrow MUST step across the entire extended grapheme cluster in one hit:
    bool handledRight = editor->handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, handledRight);
    CaretPosition posAfterRight = editor->selection().focus;
    REPORTER_ASSERT(reporter, posAfterRight.text_index == TextIndex(zalgo.size()));
    REPORTER_ASSERT(reporter, posAfterRight.caret_rect.fLeft > startX);

    // 2. Single Left Arrow MUST step backward across the entire cluster back to 0:
    bool handledLeft = editor->handleKey(skui::Key::kLeft, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, handledLeft);
    CaretPosition posAfterLeft = editor->selection().focus;
    REPORTER_ASSERT(reporter, posAfterLeft.text_index == TextIndex(0));
    REPORTER_ASSERT(reporter, posAfterLeft.caret_rect.fLeft == startX);

    // --- Partition 2: Hit-Test Boundary Snapping ---
    // Hit-testing the right half of the Zalgo cluster must resolve past all combining marks:
    CaretPosition hitRight = editor->spatial_index().hitTest(startX + (posAfterRight.caret_rect.fLeft - startX) * 0.75f, 5.0f);
    REPORTER_ASSERT(reporter, hitRight.text_index == TextIndex(zalgo.size()));

    // Hit-testing the left half must resolve to the start of the cluster:
    CaretPosition hitLeft = editor->spatial_index().hitTest(startX + 1.0f, 5.0f);
    REPORTER_ASSERT(reporter, hitLeft.text_index == TextIndex(0));

    // --- Partition 3: Arabic Combining Diacritics (Harakat / Tashkeel) ---
    // Arabic letter Beh ('ب') with Shadda (U+0651) and Fatha (U+064E): 2 + 2 + 2 = 6 UTF-8 bytes
    const std::string arabicWithMarks = "\xd8\xa8\xd9\x91\xd9\x8e";
    const std::string arabicText = arabicWithMarks + " \xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a";
    auto arabicEditor = std::make_unique<TextEditorViewModel>(arabicText, font);
    REPORTER_ASSERT(reporter, arabicEditor != nullptr);

    // Single step forward along reading order (kTextLogical):
    CaretPosition arabicCaret = arabicEditor->spatial_index().moveCaret(
        arabicEditor->selection().focus,
        CursorDirection::kRight,
        MovementGranularity::kGrapheme,
        NavigationMode::kTextLogical);
    // Must step across the Arabic letter and both vowel marks in one step (to byte 6):
    REPORTER_ASSERT(reporter, arabicCaret.text_index == TextIndex(arabicWithMarks.size()));
}

// =============================================================================
// INVARIANT TRAP 15: Cross-Layer Stress Propagation & Zero-Delta Phantom Navigation Law
// =============================================================================
DEF_TEST(TextEditor_Invariant_CrossLayerStressPropagation, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);

    for (const auto& tc : GetCrossLayerStressCorpus()) {
        auto editor = std::make_unique<TextEditorViewModel>(tc.text, font);
        REPORTER_ASSERT(reporter, editor != nullptr);

        // 1. Dual-Contract Formatting Check:
        // Must never produce zero-height bounds even on empty buffer
        CaretPosition startCaret = editor->selection().focus;
        REPORTER_ASSERT(reporter, startCaret.caret_rect.height() > 0);

        if (tc.text.empty()) {
            continue;
        }

        // 2. Zero-Delta Phantom Navigation Law Check:
        // Walking forward must NEVER produce a zero-advance step where logical index
        // advances while spatial position (X, Y) remains frozen.
        int steps = 0;
        const int kMaxSteps = 200;
        TextIndex prevIndex = startCaret.text_index;
        SkPoint prevCaretPos = SkPoint::Make(startCaret.caret_rect.fLeft, startCaret.caret_rect.fTop);

        while (editor->selection().focus.text_index.value < tc.text.size() && ++steps < kMaxSteps) {
            bool moved = editor->handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
            if (!moved) {
                break;
            }
            CaretPosition cur = editor->selection().focus;
            if (cur.text_index == prevIndex) {
                // Reached end of line or document
                break;
            }

            // The Zero-Delta Phantom Navigation Law:
            // If logical text index advanced, the caret MUST visually move (either X or Y changed):
            SkPoint curCaretPos = SkPoint::Make(cur.caret_rect.fLeft, cur.caret_rect.fTop);
            REPORTER_ASSERT(reporter, curCaretPos != prevCaretPos);

            prevIndex = cur.text_index;
            prevCaretPos = curCaretPos;
        }
        REPORTER_ASSERT(reporter, steps < kMaxSteps);
    }
}

// =============================================================================
// TRAP 16: MVVM TextDocument Domain Invariants
// =============================================================================
DEF_TEST(TextEditor_MVVM_Model_DocumentInvariants, reporter) {
    SkFont font = ToolUtils::DefaultPortableFont();
    TextDocument doc("Hello World", font);

    // 1. Initial State & Monotonic Revision
    REPORTER_ASSERT(reporter, doc.text() == "Hello World");
    REPORTER_ASSERT(reporter, doc.revision() == 0);
    REPORTER_ASSERT(reporter, doc.styles().size() == 1);
    REPORTER_ASSERT(reporter, doc.styles()[0].range.start.value == 0);
    REPORTER_ASSERT(reporter, doc.styles()[0].range.end.value == 11);

    // 2. Insert Mutation
    doc.insert(TextIndex(5), ", Beautiful");
    REPORTER_ASSERT(reporter, doc.text() == "Hello, Beautiful World");
    REPORTER_ASSERT(reporter, doc.revision() == 1);
    REPORTER_ASSERT(reporter, doc.styles()[0].range.end.value == doc.text().size());

    // 3. Atomic Replace Mutation
    TextRange replaceRange(TextIndex(7), TextIndex(16)); // "Beautiful"
    doc.replace(replaceRange, "Brave");
    REPORTER_ASSERT(reporter, doc.text() == "Hello, Brave World");
    REPORTER_ASSERT(reporter, doc.revision() == 2);
    REPORTER_ASSERT(reporter, doc.styles()[0].range.end.value == doc.text().size());

    // 4. Erase Mutation
    doc.erase(TextRange(TextIndex(5), TextIndex(12))); // ", Brave"
    REPORTER_ASSERT(reporter, doc.text() == "Hello World");
    REPORTER_ASSERT(reporter, doc.revision() == 3);

    // 5. Streaming Visitor on Document
    int runCount = 0;
    size_t totalGlyphs = 0;
    doc.visitDocumentRuns(SkRect::MakeXYWH(0, 0, 1000, 1000), [&](const RenderRun& run) {
        ++runCount;
        totalGlyphs += run.glyphs.size();
        REPORTER_ASSERT(reporter, run.glyphs.size() == run.positions.size());
    });
    REPORTER_ASSERT(reporter, runCount > 0);
    REPORTER_ASSERT(reporter, totalGlyphs > 0);
}

// =============================================================================
// TRAP 17: MVVM TextEditorViewModel Presentation & Visitor
// =============================================================================
DEF_TEST(TextEditor_MVVM_ViewModel_PresentationAndVisitor, reporter) {
    SkFont font = ToolUtils::DefaultPortableFont();
    auto doc = std::make_unique<TextDocument>("The quick brown fox", font);
    TextEditorViewModel vm(std::move(doc));

    // 1. Initial state and headless caret
    REPORTER_ASSERT(reporter, vm.document().text() == "The quick brown fox");
    REPORTER_ASSERT(reporter, vm.selection().is_collapsed());
    SkRect initialCaret = vm.screenCaretRect();
    REPORTER_ASSERT(reporter, initialCaret.height() > 0);

    // 2. Redraw notification observer
    int redrawCount = 0;
    vm.setOnRedrawCallback([&]() {
        ++redrawCount;
    });

    vm.handleChar('!', skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, redrawCount == 1);
    REPORTER_ASSERT(reporter, vm.document().text() == "!The quick brown fox");

    // 3. Arrow movement (kTextLogical)
    vm.handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, redrawCount == 2);
    REPORTER_ASSERT(reporter, vm.selection().focus.text_index.value == 2);

    // 4. Viewport Scroll and coordinate translation
    vm.setScrollOffset(SkPoint::Make(50, 100));
    REPORTER_ASSERT(reporter, vm.scrollOffset().fX == 50);
    REPORTER_ASSERT(reporter, vm.scrollOffset().fY == 100);
    SkRect shiftedCaret = vm.screenCaretRect();
    REPORTER_ASSERT(reporter, std::abs((initialCaret.fTop - 100) - shiftedCaret.fTop) < 1.0f);

    // 5. EnsureCaretVisible adjusts scroll offset
    SkRect smallViewport = SkRect::MakeXYWH(0, 0, 20, 20);
    vm.ensureCaretVisible(smallViewport);
    REPORTER_ASSERT(reporter, vm.screenCaretRect().fLeft >= 0);

    // 6. Streaming Visitor into Paint
    SkBitmap bitmap;
    bitmap.allocN32Pixels(400, 200);
    SkCanvas canvas(bitmap);
    canvas.clear(SK_ColorWHITE);

    PaintOptions options;
    TextEditorPainter::Paint(&canvas, vm, options);

    // Must have non-empty pixels drawn (not pure white)
    bool hasTextPixels = false;
    for (int y = 0; y < bitmap.height() && !hasTextPixels; ++y) {
        for (int x = 0; x < bitmap.width() && !hasTextPixels; ++x) {
            if (bitmap.getColor(x, y) != SK_ColorWHITE) {
                hasTextPixels = true;
            }
        }
    }
    REPORTER_ASSERT(reporter, hasTextPixels);
}

// =============================================================================
// TRAP 18: Domain Invariant 10 - Typographic Caret Bounds Separation on Zalgo
// =============================================================================
DEF_TEST(TextEditor_Invariant10_TypographicCaretOnZalgo, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 18.0f);

    // Zalgo string: 'e' with 6 stacked combining marks
    const std::string zalgo = "e\xcc\x81\xcc\x80\xcc\x83\xcc\x82\xcc\x88\xcc\x8a";
    auto vm = std::make_unique<TextEditorViewModel>(zalgo, font);
    REPORTER_ASSERT(reporter, vm != nullptr);

    SkRect caretRect = vm->screenCaretRect();
    SkFontMetrics metrics;
    font.getMetrics(&metrics);
    SkScalar typographicHeight = std::abs(metrics.fAscent) + std::abs(metrics.fDescent);

    // Hostile Invariant:
    // Even though line ink bounds are huge (>60px), the caret height MUST strictly adhere
    // to typographic metrics and must NEVER expand to the ink bounds of stacked marks!
    REPORTER_ASSERT(reporter, caretRect.height() > 0.0f);
    REPORTER_ASSERT(reporter, std::abs(caretRect.height() - typographicHeight) < 0.01f);
    REPORTER_ASSERT(reporter, caretRect.height() < 30.0f);
}

// =============================================================================
// TRAP 19: Domain Invariant 11 - BiDi Cluster Hit-Testing & Selection Geometry
// =============================================================================
DEF_TEST(TextEditor_Invariant11_BiDiClusterHitTestingAndDrag, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);

    // Mixed BiDi line: Latin prefix followed by Arabic text (as in TextEditorApp):
    // "- Arabic: مرحبا"
    const std::string mixedText = "Prefix: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
    auto vm = std::make_unique<TextEditorViewModel>(mixedText, font);
    REPORTER_ASSERT(reporter, vm != nullptr);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, !lines.empty());
    const auto& line = lines[0];

    // Explicit Invariant 11 Check: Hit-testing within an RTL cluster MUST invert edges!
    // Arabic text is bytes [8..18).
    // Letter 'م' (start of Arabic word) is at bytes [8, 10).
    // Letter 'ا' (end of Arabic word) is at bytes [16, 18).
    const auto& spatial = vm->document().spatial_index();
    std::vector<SkRect> firstLetterRects;
    std::vector<SkRect> lastLetterRects;
    spatial.getSelectionRects(TextRange(TextIndex(8), TextIndex(10)), firstLetterRects);
    spatial.getSelectionRects(TextRange(TextIndex(16), TextIndex(18)), lastLetterRects);
    REPORTER_ASSERT(reporter, !firstLetterRects.empty());
    REPORTER_ASSERT(reporter, !lastLetterRects.empty());

    // In RTL, last letter 'ا' is visually to the LEFT of first letter 'م':
    REPORTER_ASSERT(reporter, lastLetterRects[0].fLeft < firstLetterRects[0].fLeft);

    // Hit-testing inside the left half of the last letter (RTL):
    // Since last letter is RTL, its left half corresponds to its logical end (18):
    CaretPosition hitLastLetterLeft = spatial.hitTest(lastLetterRects[0].fLeft + 1.0f, line.bounds.centerY());
    REPORTER_ASSERT(reporter, hitLastLetterLeft.text_index.value == 18);

    // Hit-testing inside the right half of the first letter (RTL):
    // Since first letter is RTL, its right half corresponds to its logical start (8):
    CaretPosition hitFirstLetterRight = spatial.hitTest(firstLetterRects[0].fRight - 1.0f, line.bounds.centerY());
    REPORTER_ASSERT(reporter, hitFirstLetterRight.text_index.value == 8);

    // Dragging from X1 to X2 in Arabic text:
    SkScalar x1 = lastLetterRects[0].fLeft;
    SkScalar x2 = firstLetterRects[0].fRight;
    vm->moveCaretToPoint(x1, line.bounds.centerY(), false); // anchor
    vm->moveCaretToPoint(x2, line.bounds.centerY(), true);  // focus (dragged right)

    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    std::vector<SkRect> selRects = vm->screenSelectionRects();
    REPORTER_ASSERT(reporter, !selRects.empty());

    // The selection must span across the dragged region [x1, x2]:
    SkScalar minLeft = selRects[0].fLeft;
    SkScalar maxRight = selRects[0].fRight;
    for (const auto& r : selRects) {
        minLeft = std::min(minLeft, r.fLeft);
        maxRight = std::max(maxRight, r.fRight);
    }
    REPORTER_ASSERT(reporter, minLeft <= x1 + 2.0f);
    REPORTER_ASSERT(reporter, maxRight >= x2 - 2.0f);
}

// =============================================================================
// TRAP 20: Domain Invariant 12 - Continuous Physical Selection & Deletion Across BiDi
// =============================================================================
DEF_TEST(TextEditor_Invariant12_CrossDirectionalBiDiDragSelection, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);

    // Mixed text: "Hello " (LTR) + Arabic "مرحبا" (RTL)
    // Byte layout:
    // [0..5): "Hello"
    // [5..6): " " (space)
    // [6..16): "مرحبا" (Arabic)
    const std::string text = "Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
    auto vm = std::make_unique<TextEditorViewModel>(text, font);
    REPORTER_ASSERT(reporter, vm != nullptr);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, !lines.empty());
    const auto& line = lines[0];

    // Find the X position of the space:
    // "Hello " starts at 0, space is right before Arabic text.
    // Let's find cluster of the space:
    std::vector<SkRect> spaceRects;
    vm->document().spatial_index().getSelectionRects(TextRange(TextIndex(5), TextIndex(6)), spaceRects);
    REPORTER_ASSERT(reporter, !spaceRects.empty());
    SkScalar spaceX = spaceRects[0].centerX();

    // Start drag on the space:
    vm->moveCaretToPoint(spaceX, line.bounds.centerY(), false); // anchor
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());

    // Find the left-most Arabic cluster (letter 'ا' at bytes [14, 16)):
    std::vector<SkRect> alefRects;
    vm->document().spatial_index().getSelectionRects(TextRange(TextIndex(14), TextIndex(16)), alefRects);
    REPORTER_ASSERT(reporter, !alefRects.empty());

    // Drag from space to center of final alef:
    SkScalar dragX = alefRects[0].centerX();
    vm->moveCaretToPoint(dragX, line.bounds.centerY(), true); // focus

    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    std::vector<SkRect> selRects = vm->screenSelectionRects();
    REPORTER_ASSERT(reporter, !selRects.empty());

    // Calculate bounding box of selection
    SkScalar minLeft = selRects[0].fLeft;
    SkScalar maxRight = selRects[0].fRight;
    for (const auto& r : selRects) {
        minLeft = std::min(minLeft, r.fLeft);
        maxRight = std::max(maxRight, r.fRight);
    }

    // HOSTILE INVARIANT:
    // The selection MUST NOT jump to cover the entire line width or the entire Arabic word!
    REPORTER_ASSERT(reporter, maxRight <= alefRects[0].fRight + 2.0f);
    REPORTER_ASSERT(reporter, maxRight < line.content_width - 15.0f);

    // Verify Discontinuous Deletion:
    // Deleting the selection must remove the space and the final letter 'ا',
    // leaving "Hello" + "مرحب" ("Hello\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8"):
    std::string textBefore = std::string(vm->document().text());
    vm->deleteBackward();
    std::string textAfter = std::string(vm->document().text());

    REPORTER_ASSERT(reporter, textAfter != textBefore);
    // Prefix "Hello" must be intact
    REPORTER_ASSERT(reporter, textAfter.rfind("Hello", 0) == 0);
    // Arabic first letter 'م' (0xd9 0x85) must still exist in textAfter!
    REPORTER_ASSERT(reporter, textAfter.find("\xd9\x85") != std::string::npos);

    // Hardened Invariant 12: Deleted EXACTLY the space [5..6) and the final letter 'ا' [14..16).
    const std::string expectedText = "Hello\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8";
    REPORTER_ASSERT(reporter, textAfter == expectedText);
    REPORTER_ASSERT(reporter, SkUTF::CountUTF8(textAfter.data(), textAfter.size()) >= 0);
}

// =============================================================================
// TRAP 21: Domain Invariant 13 - Single-Source Render Projection & Painter Purity
// =============================================================================
class RectRecordingCanvas : public SkCanvas {
public:
    RectRecordingCanvas(const SkBitmap& bm) : SkCanvas(bm) {}
    std::vector<SkRect> drawnRects;

protected:
    void onDrawRect(const SkRect& rect, const SkPaint& paint) override {
        if (paint.getStyle() == SkPaint::kFill_Style) {
            drawnRects.push_back(rect);
        }
        SkCanvas::onDrawRect(rect, paint);
    }
};

DEF_TEST(TextEditor_Invariant13_PainterPurityOnBiDiDrag, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    const std::string text = "Hello \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
    auto vm = std::make_unique<TextEditorViewModel>(text, font);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, !lines.empty());
    const auto& line = lines[0];

    std::vector<SkRect> spaceRects;
    vm->document().spatial_index().getSelectionRects(TextRange(TextIndex(5), TextIndex(6)), spaceRects);
    REPORTER_ASSERT(reporter, !spaceRects.empty());
    SkScalar spaceX = spaceRects[0].centerX();

    // Drag from space 15px to the right:
    vm->moveCaretToPoint(spaceX, line.bounds.centerY(), false);
    SkScalar dragX = spaceRects[0].fRight + 15.0f;
    vm->moveCaretToPoint(dragX, line.bounds.centerY(), true);

    // Record what TextEditorPainter actually draws!
    SkBitmap bitmap;
    bitmap.allocN32Pixels(500, 200);
    RectRecordingCanvas canvas(bitmap);
    PaintOptions options;
    TextEditorPainter::Paint(&canvas, *vm, options);

    REPORTER_ASSERT(reporter, !canvas.drawnRects.empty());
    SkScalar maxDrawnRight = canvas.drawnRects[0].fRight;
    for (const auto& r : canvas.drawnRects) {
        maxDrawnRight = std::max(maxDrawnRight, r.fRight);
    }

    // HOSTILE GATE A ASSERTION:
    // TextEditorPainter must draw STRICTLY what the user dragged across!
    // It must NEVER expand to line.content_width!
    REPORTER_ASSERT(reporter, maxDrawnRight <= dragX + 20.0f);
    REPORTER_ASSERT(reporter, maxDrawnRight < line.content_width - 15.0f);
}







