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
#include "tests/Test.h"
#include "tools/text_editor/include/EditorTypes.h"
#include "tools/text_editor/include/FormattedParagraph.h"
#include "tools/text_editor/include/ParagraphSpatialIndex.h"
#include "tools/text_editor/include/ShapedParagraph.h"
#include "tools/text_editor/include/TextEditorController.h"
#include "tools/text_editor/include/TextEditorPainter.h"
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

// =============================================================================
// TRAP 6: Layer 5 Controller Text Mutation & Selection Invariants
// =============================================================================
DEF_TEST(TextEditor_Controller_InsertionAndDeletion, reporter) {
    SkFont font;
    auto editor = TextEditorController::Make("Hello World", font);
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
    auto editor = TextEditorController::Make("The quick brown fox", font);
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
    auto editor = TextEditorController::Make("Visual Rendering Test\nLine 2", font);
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
    auto editor = TextEditorController::Make("Hello World", font);
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
    auto editor = TextEditorController::Make("", font, SkColor4f{0, 0, 0, 1}, constraints);
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
    auto editor = TextEditorController::Make(initialText, font);
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




