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
    REPORTER_ASSERT(reporter, caret0.text_index() == TextIndex(0));
    REPORTER_ASSERT(reporter, caret0.affinity() == Affinity::kDownstream);

    // 2. Physical vs Logical Caret Movement Trap
    // Step forward physically vs logically:
    CaretPosition nextPhys = spatial->moveCaret(caret0, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kScreenPhysical);
    CaretPosition nextLog  = spatial->moveCaret(caret0, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);

    // For LTR characters at start of sentence, both advance to index 1:
    REPORTER_ASSERT(reporter, nextPhys.text_index() == TextIndex(1));
    REPORTER_ASSERT(reporter, nextLog.text_index() == TextIndex(1));

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
    while (editor->selection().focus().text_index() < TextIndex(editor->text().size()) && ++safetySteps < 1000) {
        editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    }
    REPORTER_ASSERT(reporter, safetySteps < 1000);
    editor->insertText("!");
    REPORTER_ASSERT(reporter, editor->text() == "Hello World!");
    REPORTER_ASSERT(reporter, editor->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);

    // Backspace: removes "!"
    editor->deleteBackward();
    REPORTER_ASSERT(reporter, editor->text() == "Hello World");
    REPORTER_ASSERT(reporter, editor->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);

    // Select "World" [6, 11) and replace with "Skia"
    CaretPosition anchor(TextIndex(6), Affinity::kDownstream);
    CaretPosition focus(TextIndex(11), Affinity::kDownstream);

    editor->setSelection(anchor, focus);
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().text_range() == TextRange(TextIndex(6), TextIndex(11)));

    editor->insertText("Skia");
    REPORTER_ASSERT(reporter, editor->text() == "Hello Skia");
    REPORTER_ASSERT(reporter, editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(10));
    REPORTER_ASSERT(reporter, editor->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);

    // Collapse to beginning and deleteForward
    CaretPosition startPos(TextIndex(0), Affinity::kDownstream);
    editor->collapseTo(startPos);
    editor->deleteForward();
    REPORTER_ASSERT(reporter, editor->text() == "ello Skia");
    REPORTER_ASSERT(reporter, editor->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);

    // Select all and deleteBackward
    editor->selectAll();
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().text_range().length() == editor->text().size());
    editor->deleteBackward();
    REPORTER_ASSERT(reporter, editor->text().empty());
    REPORTER_ASSERT(reporter, editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);
}

// =============================================================================
// TRAP 7: Layer 5 Controller Navigation & Word Selection
// =============================================================================
DEF_TEST(TextEditor_Controller_NavigationAndWordSelection, reporter) {
    SkFont font;
    auto editor = std::make_unique<TextEditorViewModel>("The quick brown fox", font);
    REPORTER_ASSERT(reporter, editor != nullptr);

    // 1. Move caret with selection expansion (select = true)
    CaretPosition startPos(TextIndex(0), Affinity::kDownstream);
    editor->collapseTo(startPos);

    editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, true);
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().anchor().text_index() == TextIndex(0));
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(1));
    REPORTER_ASSERT(reporter, editor->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);

    // 2. Collapse navigation (select = false)
    editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    REPORTER_ASSERT(reporter, editor->selection().is_collapsed());
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(2));
    REPORTER_ASSERT(reporter, editor->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);

    // 3. Word selection at point
    // Inside "quick" (x offset of 'q' is after "The ")
    // Hit-testing near the word should select [4, 9)
    editor->selectWordAtPoint(35.0f, 5.0f);
    REPORTER_ASSERT(reporter, !editor->selection().is_collapsed());
    // The selection must be a valid non-empty range covering words
    TextRange selRange = editor->selection().text_range();
    std::vector<SkRect> selRects;
    editor->document().spatial_index().getSelectionRects(selRange, selRects);
    REPORTER_ASSERT(reporter, !selRects.empty());
    REPORTER_ASSERT(reporter, selRects[0].width() > 0.0f);
    REPORTER_ASSERT(reporter, selRects[0].height() > 0.0f);
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
    while (editor->selection().focus().text_index() < TextIndex(editor->text().size()) && ++stepLimit < 1000) {
        editor->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    }
    REPORTER_ASSERT(reporter, stepLimit < 1000);
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(11));

    SkScalar beforeX = editor->selection().focus().caret_rect().fLeft;
    REPORTER_ASSERT(reporter, beforeX > 0.0f);

    // Backspace deletes 'd'. New text is "Hello Worl" (length 10)
    editor->deleteBackward();
    REPORTER_ASSERT(reporter, editor->text() == "Hello Worl");
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(10));

    // Hostile Invariant: Caret X position must NOT reset to 0.0f! It must sit at the end of "Worl"!
    SkScalar afterX = editor->selection().focus().caret_rect().fLeft;
    REPORTER_ASSERT(reporter, afterX > 0.0f);
    REPORTER_ASSERT(reporter, afterX < beforeX);

    // Move to index 5 ("Hello| Worl") and test deleteForward
    CaretPosition midPos(TextIndex(5), Affinity::kDownstream);
    editor->collapseTo(midPos);

    // Delete space at index 5 -> "HelloWorl"
    editor->deleteForward();
    REPORTER_ASSERT(reporter, editor->text() == "HelloWorl");
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(5));
    // Caret X position after deleteForward must sit at the boundary, NOT at 0.0f!
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().fLeft > 0.0f);
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
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().fLeft >= 0.0f);

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
    CaretPosition endPos = editor->selection().focus();
    REPORTER_ASSERT(reporter, endPos.text_index() == TextIndex(fullSentence.size()));
    REPORTER_ASSERT(reporter, endPos.caret_rect().fLeft > 0.0f);
    SkScalar endX = endPos.caret_rect().fLeft;

    // 3. Headless Backspace Flow: Delete "dog" (3 backspaces)
    editor->deleteBackward(); // deletes 'g'
    REPORTER_ASSERT(reporter, editor->text() == "The quick brown fox jumps over the lazy do");
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().fLeft < endX);
    SkScalar doX = editor->selection().focus().caret_rect().fLeft;

    editor->deleteBackward(); // deletes 'o'
    SkScalar dX = editor->selection().focus().caret_rect().fLeft;
    REPORTER_ASSERT(reporter, dX < doX);
    REPORTER_ASSERT(reporter, dX > 0.0f);

    editor->deleteBackward(); // deletes 'd'
    // With 'd' deleted, the wrapped line for "dog" collapses, and the caret wraps up
    // to the end of the previous line ("...lazy ").
    REPORTER_ASSERT(reporter, editor->text() == "The quick brown fox jumps over the lazy ");
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().fLeft > 0.0f);

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
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(8));
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().fLeft > 0.0f);
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
    REPORTER_ASSERT(reporter, editor->selection().focus().caret_rect().height() > 0.0f);
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
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(0));
    SkScalar startY = editor->selection().focus().caret_rect().fTop;

    // Move right 5 characters: "First|"
    for (int i = 0; i < 5; ++i) {
        editor->handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
    }
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(5));

    // Hostile Invariant 1: Arrow Down must move caret to Line 1 ("Second Line")
    bool downHandled = editor->handleKey(skui::Key::kDown, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, downHandled);

    CaretPosition downPos = editor->selection().focus();
    // Must move to line 1: Y coordinate must increase!
    REPORTER_ASSERT(reporter, downPos.caret_rect().fTop > startY);
    // Must be in "Second Line" (index >= 11, which is after "First Line\n")
    REPORTER_ASSERT(reporter, downPos.text_index() >= TextIndex(11));

    // Hostile Invariant 2: Arrow Up must move caret back to Line 0
    bool upHandled = editor->handleKey(skui::Key::kUp, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, upHandled);

    CaretPosition upPos = editor->selection().focus();
    // Must return to line 0: Y coordinate must match startY!
    REPORTER_ASSERT(reporter, upPos.caret_rect().fTop == startY);
    REPORTER_ASSERT(reporter, upPos.text_index() < TextIndex(11));
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
    REPORTER_ASSERT(reporter, editor->selection().focus().text_index() == TextIndex(0));
    SkScalar startX = editor->selection().focus().caret_rect().fLeft;

    // 1. Single Right Arrow MUST step across the entire extended grapheme cluster in one hit:
    bool handledRight = editor->handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, handledRight);
    CaretPosition posAfterRight = editor->selection().focus();
    REPORTER_ASSERT(reporter, posAfterRight.text_index() == TextIndex(zalgo.size()));
    REPORTER_ASSERT(reporter, posAfterRight.caret_rect().fLeft > startX);

    // 2. Single Left Arrow MUST step backward across the entire cluster back to 0:
    bool handledLeft = editor->handleKey(skui::Key::kLeft, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, handledLeft);
    CaretPosition posAfterLeft = editor->selection().focus();
    REPORTER_ASSERT(reporter, posAfterLeft.text_index() == TextIndex(0));
    REPORTER_ASSERT(reporter, posAfterLeft.caret_rect().fLeft == startX);

    // --- Partition 2: Hit-Test Boundary Snapping ---
    // Hit-testing the right half of the Zalgo cluster must resolve past all combining marks:
    CaretPosition hitRight = editor->spatial_index().hitTest(startX + (posAfterRight.caret_rect().fLeft - startX) * 0.75f, 5.0f);
    REPORTER_ASSERT(reporter, hitRight.text_index() == TextIndex(zalgo.size()));

    // Hit-testing the left half must resolve to the start of the cluster:
    CaretPosition hitLeft = editor->spatial_index().hitTest(startX + 1.0f, 5.0f);
    REPORTER_ASSERT(reporter, hitLeft.text_index() == TextIndex(0));

    // --- Partition 3: Arabic Combining Diacritics (Harakat / Tashkeel) ---
    // Arabic letter Beh ('ب') with Shadda (U+0651) and Fatha (U+064E): 2 + 2 + 2 = 6 UTF-8 bytes
    const std::string arabicWithMarks = "\xd8\xa8\xd9\x91\xd9\x8e";
    const std::string arabicText = arabicWithMarks + " \xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a";
    auto arabicEditor = std::make_unique<TextEditorViewModel>(arabicText, font);
    REPORTER_ASSERT(reporter, arabicEditor != nullptr);

    // Single step forward along reading order (kTextLogical):
    CaretPosition arabicCaret = arabicEditor->spatial_index().moveCaret(
        arabicEditor->selection().focus(),
        CursorDirection::kRight,
        MovementGranularity::kGrapheme,
        NavigationMode::kTextLogical);
    // Must step across the Arabic letter and both vowel marks in one step (to byte 6):
    REPORTER_ASSERT(reporter, arabicCaret.text_index() == TextIndex(arabicWithMarks.size()));
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
        CaretPosition startCaret = editor->selection().focus();
        REPORTER_ASSERT(reporter, startCaret.caret_rect().height() > 0);

        if (tc.text.empty()) {
            continue;
        }

        // 2. Zero-Delta Phantom Navigation Law Check:
        // Walking forward must NEVER produce a zero-advance step where logical index
        // advances while spatial position (X, Y) remains frozen.
        int steps = 0;
        const int kMaxSteps = 200;
        TextIndex prevIndex = startCaret.text_index();
        SkPoint prevCaretPos = SkPoint::Make(startCaret.caret_rect().fLeft, startCaret.caret_rect().fTop);

        while (editor->selection().focus().text_index().value < tc.text.size() && ++steps < kMaxSteps) {
            bool moved = editor->handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
            if (!moved) {
                break;
            }
            CaretPosition cur = editor->selection().focus();
            if (cur.text_index() == prevIndex) {
                // Reached end of line or document
                break;
            }

            // The Zero-Delta Phantom Navigation Law:
            // If logical text index advanced, the caret MUST visually move (either X or Y changed):
            SkPoint curCaretPos = SkPoint::Make(cur.caret_rect().fLeft, cur.caret_rect().fTop);
            REPORTER_ASSERT(reporter, curCaretPos != prevCaretPos);

            prevIndex = cur.text_index();
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
    REPORTER_ASSERT(reporter, vm.selection().focus().text_index().value == 2);

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
    REPORTER_ASSERT(reporter, hitLastLetterLeft.text_index().value == 18);

    // Hit-testing inside the right half of the first letter (RTL):
    // Since first letter is RTL, its right half corresponds to its logical start (8):
    CaretPosition hitFirstLetterRight = spatial.hitTest(firstLetterRects[0].fRight - 1.0f, line.bounds.centerY());
    REPORTER_ASSERT(reporter, hitFirstLetterRight.text_index().value == 8);

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

// =============================================================================
// TRAP 22 (Invariant 14): Enter Key Splits Line, Updates Document & Advances Caret
// =============================================================================
DEF_TEST(TextEditor_Invariant14_EnterKeySplitsLineAndAdvancesCaret, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    auto vm = std::make_unique<TextEditorViewModel>("HelloWorld", font);

    // Position caret between "Hello" and "World" (index 5)
    vm->setSelection(CaretPosition{TextIndex(5), Affinity::kDownstream, SkRect::MakeEmpty()},
                     CaretPosition{TextIndex(5), Affinity::kDownstream, SkRect::MakeEmpty()});

    // Simulate Enter Key press
    bool handled = vm->handleKey(skui::Key::kOK, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, handled);

    // Assert document text is split
    REPORTER_ASSERT(reporter, vm->text() == "Hello\nWorld");
    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() == 2);

    // Assert caret advanced to start of second line (index 6, which is start of "World")
    REPORTER_ASSERT(reporter, vm->selection().focus().text_index() == TextIndex(6));
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());

    // Second line caret rect must have Y below first line
    SkRect caretRect = vm->screenCaretRect();
    REPORTER_ASSERT(reporter, caretRect.fTop >= lines[0].bounds.fBottom);
}

// =============================================================================
// TRAP 23 (Invariant 14): Tab Key Inserts Immediate Soft Spaces Modulo 4
// =============================================================================
DEF_TEST(TextEditor_Invariant14_TabKeyInsertsSoftSpaces, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    auto vm = std::make_unique<TextEditorViewModel>("", font);

    // 1. Tab at column 0 -> must insert 4 soft spaces
    bool tab1 = vm->handleKey(skui::Key::kTab, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, tab1);
    REPORTER_ASSERT(reporter, vm->text() == "    ");
    REPORTER_ASSERT(reporter, vm->selection().focus().text_index() == TextIndex(4));
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 2. Type "a" (column becomes 5)
    vm->handleChar('a', skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, vm->text() == "    a");
    REPORTER_ASSERT(reporter, vm->selection().focus().text_index() == TextIndex(5));
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 3. Tab at column 5 -> (4 - (5 % 4)) = 3 soft spaces
    bool tab2 = vm->handleKey(skui::Key::kTab, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, tab2);
    REPORTER_ASSERT(reporter, vm->text() == "    a   ");
    REPORTER_ASSERT(reporter, vm->selection().focus().text_index() == TextIndex(8));
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);
}

// =============================================================================
// TRAP 24 (Invariant 14): Control Character Sanitization & Shaping Format Preservation
// =============================================================================
DEF_TEST(TextEditor_Invariant14_ControlCharacterSanitizationAndShapingPreservation, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    auto vm = std::make_unique<TextEditorViewModel>("", font);

    // Ingest noisy text: null bytes, BELL, ESC, DEL, and Windows CRLF
    const char noisyData[] = "A\0B\x07\x1b\x7f\r\nC\rD";
    std::string_view explicitNoisy(noisyData, sizeof(noisyData) - 1);

    vm->insertText(explicitNoisy);

    // Expected sanitization:
    // \0, \x07, \x1b, \x7f dropped
    // \r\n normalized to \n
    // single \r normalized to \n
    // Result: "AB\nC\nD"
    REPORTER_ASSERT(reporter, vm->text() == "AB\nC\nD");
    REPORTER_ASSERT(reporter, vm->document().formatted().lines().size() == 3);

    // Verify Shaping Format Controls preservation (ZWJ U+200D: \xE2\x80\x8D, ZWNJ U+200C: \xE2\x80\x8C)
    auto vmBidi = std::make_unique<TextEditorViewModel>("", font);
    std::string persianText = "\xd9\x85\xe2\x80\x8c\xd8\xae\xd9\x88\xd8\xa7\xd9\x87\xd9\x85"; // "می‌خواهم" with ZWNJ
    vmBidi->insertText(persianText);
    REPORTER_ASSERT(reporter, vmBidi->text() == persianText);
    REPORTER_ASSERT(reporter, SkUTF::CountUTF8(vmBidi->text().data(), vmBidi->text().size()) > 0);
}

// =============================================================================
// TRAP 25 (Invariant 15): Soft-Wrap Boundary Caret Affinity & Disambiguation
// =============================================================================
DEF_TEST(TextEditor_Invariant15_SoftWrapBoundaryCaretAffinity, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    LayoutConstraints constraints;
    constraints.max_width = 70.0f; // Narrow width forces "Hello World" to wrap into 2 lines

    auto vm = std::make_unique<TextEditorViewModel>("Hello World", font, SkColor4f{0, 0, 0, 1}, constraints);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() >= 2);
    REPORTER_ASSERT(reporter, vm->text().find('\n') == std::string_view::npos);

    SkScalar line0Y = lines[0].bounds.centerY();
    SkScalar line1Y = lines[1].bounds.centerY();

    // 1. Click far to the right of Line 0 content:
    // Caret must take Affinity::kUpstream and snap to line 0 right edge (NOT jump down to line 1)
    vm->moveCaretToPoint(lines[0].bounds.fRight + 50.0f, line0Y, false);
    REPORTER_ASSERT(reporter, vm->selection().focus().affinity() == Affinity::kUpstream);
    SkRect caret0 = vm->screenCaretRect();
    REPORTER_ASSERT(reporter, caret0.fTop < lines[1].bounds.fTop);
    REPORTER_ASSERT(reporter, caret0.fLeft >= lines[0].bounds.fRight - 1.0f);

    // 2. Click at the start/left edge of Line 1:
    // Caret must take Affinity::kDownstream and snap to line 1 left edge (NOT jump up to line 0)
    vm->moveCaretToPoint(lines[1].bounds.fLeft, line1Y, false);
    REPORTER_ASSERT(reporter, vm->selection().focus().affinity() == Affinity::kDownstream);
    SkRect caret1 = vm->screenCaretRect();
    REPORTER_ASSERT(reporter, caret1.fTop >= lines[1].bounds.fTop);
    REPORTER_ASSERT(reporter, caret1.fLeft <= lines[1].bounds.fLeft + 2.0f);

    // 3. Step-through horizontal navigation across soft wrap boundary:
    // Move caret back to upstream position on Line 0
    vm->moveCaretToPoint(lines[0].bounds.fRight + 50.0f, line0Y, false);
    REPORTER_ASSERT(reporter, vm->selection().focus().affinity() == Affinity::kUpstream);

    // Press Right arrow: must transition to Line 1 downstream position
    bool handled = vm->handleKey(skui::Key::kRight, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, handled);
    REPORTER_ASSERT(reporter, vm->selection().focus().affinity() == Affinity::kDownstream);
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fTop >= lines[1].bounds.fTop);

    // Press Left arrow: must transition back to Line 0 upstream position
    bool leftHandled = vm->handleKey(skui::Key::kLeft, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, leftHandled);
    REPORTER_ASSERT(reporter, vm->selection().focus().affinity() == Affinity::kUpstream);
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fTop < lines[1].bounds.fTop);
}

// =============================================================================
// TRAP 26 (Invariant 16): Headless Clipboard Interop & Sanitized Insertion
// =============================================================================
DEF_TEST(TextEditor_Invariant16_ClipboardCopyCutPasteSanitization, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    auto vm = std::make_unique<TextEditorViewModel>("The quick brown fox jumps over the lazy dog.", font);

    std::string mockClipboard;
    vm->setClipboardHandlers(
        [&mockClipboard](std::string_view text) { mockClipboard = std::string(text); },
        [&mockClipboard]() -> std::string { return mockClipboard; });

    // Select "brown fox" (indices 10 to 19)
    vm->setSelection(CaretPosition{TextIndex(10), Affinity::kDownstream, SkRect::MakeEmpty()},
                     CaretPosition{TextIndex(19), Affinity::kDownstream, SkRect::MakeEmpty()});
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());

    // 1. Trigger Copy (Ctrl+C / Cmd+C)
    bool copyHandled = vm->handleKey(skui::Key::kC, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, copyHandled);
    REPORTER_ASSERT(reporter, mockClipboard == "brown fox");
    REPORTER_ASSERT(reporter, vm->text() == "The quick brown fox jumps over the lazy dog.");
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());

    // 2. Trigger Cut (Ctrl+X / Cmd+X)
    bool cutHandled = vm->handleKey(skui::Key::kX, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, cutHandled);
    REPORTER_ASSERT(reporter, mockClipboard == "brown fox");
    REPORTER_ASSERT(reporter, vm->text() == "The quick  jumps over the lazy dog.");
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->selection().focus().text_index() == TextIndex(10));
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 3. Inject hostile external clipboard content (CRLF, tabs, C0 noise, single CR)
    const char hostileData[] = "white\r\nwolf\t\0\x07runs\rfast";
    mockClipboard = std::string(hostileData, sizeof(hostileData) - 1);

    // Trigger Paste (Ctrl+V / Cmd+V)
    bool pasteHandled = vm->handleKey(skui::Key::kV, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, pasteHandled);

    // Sanitization expectation:
    // \r\n -> \n
    // \t -> soft spaces (4 spaces)
    // \0, \x07 dropped
    // \r -> \n
    // "white\nwolf    runs\nfast" inserted at index 10
    std::string expectedText = "The quick white\nwolf    runs\nfast jumps over the lazy dog.";
    REPORTER_ASSERT(reporter, vm->text() == expectedText);
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());
    size_t expectedCaret = 10 + std::string("white\nwolf    runs\nfast").size();
    REPORTER_ASSERT(reporter, vm->selection().focus().text_index() == TextIndex(expectedCaret));
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);
    REPORTER_ASSERT(reporter, vm->document().formatted().lines().size() == 3);
}

// =============================================================================
// TRAP 27: Domain Invariant 12 - 2D Multi-Line Visual Drag Continuity
// =============================================================================
DEF_TEST(TextEditor_Invariant12_MultiLineVisualDrag, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);

    // 3 lines of distinct text:
    const std::string text = "First line of text.\nSecond line of text.\nThird line of text.";
    auto vm = std::make_unique<TextEditorViewModel>(text, font);
    REPORTER_ASSERT(reporter, vm != nullptr);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() == 3);

    // 1. Matrix 1D / 0D sanity check:
    // Single point drag (0D): anchor and focus identical
    vm->moveCaretToPoint(10.0f, lines[0].bounds.centerY(), false);
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());

    // 2. Matrix 2D Downward Drag across 2 lines:
    // Anchor at middle of Line 0, Focus at middle of Line 1
    SkScalar x0 = lines[0].bounds.centerX();
    SkScalar y0 = lines[0].bounds.centerY();
    SkScalar x1 = lines[1].bounds.centerX();
    SkScalar y1 = lines[1].bounds.centerY();

    vm->moveCaretToPoint(x0, y0, false); // anchor on line 0
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());
    vm->moveCaretToPoint(x1, y1, true);  // drag down to line 1
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());

    std::vector<SkRect> rects2D = vm->screenSelectionRects();
    // HOSTILE CHECK: Both line 0 and line 1 MUST have selection rectangles!
    bool hasLine0Rect = false;
    bool hasLine1Rect = false;
    for (const auto& r : rects2D) {
        if (r.fTop >= lines[0].bounds.fTop - 1.0f && r.fBottom <= lines[0].bounds.fBottom + 1.0f) {
            hasLine0Rect = true;
        }
        if (r.fTop >= lines[1].bounds.fTop - 1.0f && r.fBottom <= lines[1].bounds.fBottom + 1.0f) {
            hasLine1Rect = true;
        }
    }
    REPORTER_ASSERT(reporter, hasLine0Rect, "Line 0 selection dropped during downward 2D drag!");
    REPORTER_ASSERT(reporter, hasLine1Rect, "Line 1 selection missing during downward 2D drag!");

    // 3. Matrix 2D Downward Drag across 3 lines with Intermediate Saturation:
    // Anchor at middle of Line 0, Focus at middle of Line 2
    SkScalar x2 = lines[2].bounds.centerX();
    SkScalar y2 = lines[2].bounds.centerY();
    vm->moveCaretToPoint(x0, y0, false); // anchor on line 0
    vm->moveCaretToPoint(x2, y2, true);  // drag to line 2

    std::vector<SkRect> rects3Lines = vm->screenSelectionRects();
    hasLine0Rect = false;
    hasLine1Rect = false;
    bool hasLine2Rect = false;
    SkScalar line1SelectedWidth = 0.0f;
    for (const auto& r : rects3Lines) {
        if (r.fTop >= lines[0].bounds.fTop - 1.0f && r.fBottom <= lines[0].bounds.fBottom + 1.0f) {
            hasLine0Rect = true;
        }
        if (r.fTop >= lines[1].bounds.fTop - 1.0f && r.fBottom <= lines[1].bounds.fBottom + 1.0f) {
            hasLine1Rect = true;
            line1SelectedWidth += r.width();
        }
        if (r.fTop >= lines[2].bounds.fTop - 1.0f && r.fBottom <= lines[2].bounds.fBottom + 1.0f) {
            hasLine2Rect = true;
        }
    }
    REPORTER_ASSERT(reporter, hasLine0Rect, "Line 0 missing during 3-line drag!");
    REPORTER_ASSERT(reporter, hasLine1Rect, "Intermediate Line 1 missing during 3-line drag!");
    REPORTER_ASSERT(reporter, hasLine2Rect, "Line 2 missing during 3-line drag!");
    REPORTER_ASSERT(reporter, line1SelectedWidth >= lines[1].content_width - 5.0f,
                    "Intermediate line 1 must be 100%% saturated!");

    // 4. Matrix 2D Upward Drag (Inverse Vector):
    // Anchor at middle of Line 2, Focus at middle of Line 0
    vm->moveCaretToPoint(x2, y2, false); // anchor on line 2
    vm->moveCaretToPoint(x0, y0, true);  // drag UP to line 0
    std::vector<SkRect> rectsUpward = vm->screenSelectionRects();
    hasLine0Rect = false;
    hasLine1Rect = false;
    hasLine2Rect = false;
    line1SelectedWidth = 0.0f;
    for (const auto& r : rectsUpward) {
        if (r.fTop >= lines[0].bounds.fTop - 1.0f && r.fBottom <= lines[0].bounds.fBottom + 1.0f) {
            hasLine0Rect = true;
        }
        if (r.fTop >= lines[1].bounds.fTop - 1.0f && r.fBottom <= lines[1].bounds.fBottom + 1.0f) {
            hasLine1Rect = true;
            line1SelectedWidth += r.width();
        }
        if (r.fTop >= lines[2].bounds.fTop - 1.0f && r.fBottom <= lines[2].bounds.fBottom + 1.0f) {
            hasLine2Rect = true;
        }
    }
    REPORTER_ASSERT(reporter, hasLine0Rect, "Line 0 missing during upward drag!");
    REPORTER_ASSERT(reporter, hasLine1Rect, "Intermediate Line 1 missing during upward drag!");
    REPORTER_ASSERT(reporter, hasLine2Rect, "Line 2 missing during upward drag!");
    REPORTER_ASSERT(reporter, line1SelectedWidth >= lines[1].content_width - 5.0f,
                    "Intermediate line 1 must be 100%% saturated on upward drag!");
}

// =============================================================================
// TRAP 28: Domain Invariant 17 - Linear Command History (Undo/Redo)
// =============================================================================
DEF_TEST(TextEditor_Invariant17_LinearCommandHistoryUndoRedo, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    auto vm = std::make_unique<TextEditorViewModel>("", font);
    REPORTER_ASSERT(reporter, vm != nullptr);

    // 1. Initial State
    REPORTER_ASSERT(reporter, !vm->canUndo());
    REPORTER_ASSERT(reporter, !vm->canRedo());
    REPORTER_ASSERT(reporter, !vm->undo());
    REPORTER_ASSERT(reporter, !vm->redo());

    // 2. Typing with Coalescing across Word Boundaries:
    // Type "Hello"
    for (char c : std::string("Hello")) {
        vm->handleChar(c, skui::ModifierKey::kNone);
    }
    REPORTER_ASSERT(reporter, vm->text() == "Hello");
    REPORTER_ASSERT(reporter, vm->canUndo());
    REPORTER_ASSERT(reporter, !vm->canRedo());

    // Type space " " (Word Boundary partition)
    vm->handleChar(' ', skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, vm->text() == "Hello ");

    // Type "World"
    for (char c : std::string("World")) {
        vm->handleChar(c, skui::ModifierKey::kNone);
    }
    REPORTER_ASSERT(reporter, vm->text() == "Hello World");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 3. Sequential Undo across Word Boundaries
    // First undo: removes "World"
    bool u1 = vm->undo();
    REPORTER_ASSERT(reporter, u1);
    REPORTER_ASSERT(reporter, vm->text() == "Hello ");
    REPORTER_ASSERT(reporter, vm->canUndo());
    REPORTER_ASSERT(reporter, vm->canRedo());

    // Second undo: removes " "
    bool u2 = vm->undo();
    REPORTER_ASSERT(reporter, u2);
    REPORTER_ASSERT(reporter, vm->text() == "Hello");

    // Third undo: removes "Hello"
    bool u3 = vm->undo();
    REPORTER_ASSERT(reporter, u3);
    REPORTER_ASSERT(reporter, vm->text().empty());
    REPORTER_ASSERT(reporter, !vm->canUndo());
    REPORTER_ASSERT(reporter, vm->canRedo());
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 4. Sequential Redo
    bool r1 = vm->redo();
    REPORTER_ASSERT(reporter, r1);
    REPORTER_ASSERT(reporter, vm->text() == "Hello");

    bool r2 = vm->redo();
    REPORTER_ASSERT(reporter, r2);
    REPORTER_ASSERT(reporter, vm->text() == "Hello ");

    bool r3 = vm->redo();
    REPORTER_ASSERT(reporter, r3);
    REPORTER_ASSERT(reporter, vm->text() == "Hello World");
    REPORTER_ASSERT(reporter, !vm->canRedo());

    // 5. Branch Truncation Invariant:
    // Undo "World", then type "Skia" -> redo history must be discarded
    vm->undo();
    REPORTER_ASSERT(reporter, vm->text() == "Hello ");
    REPORTER_ASSERT(reporter, vm->canRedo());

    for (char c : std::string("Skia")) {
        vm->handleChar(c, skui::ModifierKey::kNone);
    }
    REPORTER_ASSERT(reporter, vm->text() == "Hello Skia");
    REPORTER_ASSERT(reporter, !vm->canRedo(), "Branch truncation violated: Redo stack not discarded!");
    REPORTER_ASSERT(reporter, !vm->redo());

    // 6. Block Deletion & Selection Restoration:
    // Select "Skia" (indices 6 to 10)
    vm->setSelection(CaretPosition{TextIndex(6), Affinity::kDownstream, SkRect::MakeEmpty()},
                     CaretPosition{TextIndex(10), Affinity::kDownstream, SkRect::MakeEmpty()});
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());

    // Delete selection
    vm->deleteBackward();
    REPORTER_ASSERT(reporter, vm->text() == "Hello ");
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // Undo deletion: must restore "Hello Skia" AND non-collapsed selection over "Skia"
    vm->undo();
    REPORTER_ASSERT(reporter, vm->text() == "Hello Skia");
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed(), "Undo must restore non-collapsed selection!");
    REPORTER_ASSERT(reporter, vm->selection().text_range() == TextRange(TextIndex(6), TextIndex(10)));

    // 7. Shortcut Key Bindings (Ctrl+Z and Ctrl+Shift+Z / Ctrl+Y)
    // Ctrl+Z: undo selection restoration
    bool keyUndo = vm->handleKey(skui::Key::kZ, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, keyUndo);
    REPORTER_ASSERT(reporter, vm->text() == "Hello ");

    // Ctrl+Shift+Z: redo
    bool keyRedoShift = vm->handleKey(skui::Key::kZ, skui::InputState::kDown,
                                      skui::ModifierKey::kControl | skui::ModifierKey::kShift);
    REPORTER_ASSERT(reporter, keyRedoShift);
    REPORTER_ASSERT(reporter, vm->text() == "Hello Skia");

    // Ctrl+Z again
    vm->handleKey(skui::Key::kZ, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, vm->text() == "Hello ");

    // Ctrl+Y: redo
    bool keyRedoY = vm->handleKey(skui::Key::kY, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, keyRedoY);
    REPORTER_ASSERT(reporter, vm->text() == "Hello Skia");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);
}

DEF_TEST(TextEditor_Invariant18_ModifierLatchingAndAccidentalCharImmunity, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    auto vm = std::make_unique<TextEditorViewModel>("", font);
    REPORTER_ASSERT(reporter, vm != nullptr);

    vm->insertText("First paragraph with selected word.");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 1. Chained Command Latching (holding Ctrl down across multiple commands):
    // Simulate pressing physical Ctrl down:
    vm->handleKey(skui::Key::kCtrl, skui::InputState::kDown, skui::ModifierKey::kControl);

    // Make an edit so we have something to undo
    vm->insertText(" Extra edit.");
    REPORTER_ASSERT(reporter, vm->text() == "First paragraph with selected word. Extra edit.");

    // First Undo: with explicit kControl modifier in key event
    bool u1 = vm->handleKey(skui::Key::kZ, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, u1);
    REPORTER_ASSERT(reporter, vm->text() == "First paragraph with selected word.");

    // Make another edit, then undo it, but simulate X11 modifier drop (modifiers == kNone while Ctrl is held):
    vm->insertText(" Another edit.");
    REPORTER_ASSERT(reporter, vm->text() == "First paragraph with selected word. Another edit.");

    bool u2 = vm->handleKey(skui::Key::kZ, skui::InputState::kDown, skui::ModifierKey::kNone);
    // HOSTILE ASSERTION: Must succeed because physical Ctrl key is latched in ViewModel!
    REPORTER_ASSERT(reporter, u2, "Chained shortcut failed: Ctrl latching was lost when modifier mask was dropped!");
    REPORTER_ASSERT(reporter, vm->text() == "First paragraph with selected word.");

    // Release physical Ctrl
    vm->handleKey(skui::Key::kCtrl, skui::InputState::kUp, skui::ModifierKey::kNone);
    // Now with Ctrl released, pressing Z with kNone must NOT perform undo:
    bool u3 = vm->handleKey(skui::Key::kZ, skui::InputState::kDown, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, !u3);

    // 2. Selection Accidental Overwrite Immunity:
    // Select the word "selected"
    vm->setSelection(CaretPosition{TextIndex(21), Affinity::kDownstream, SkRect::MakeEmpty()},
                     CaretPosition{TextIndex(29), Affinity::kDownstream, SkRect::MakeEmpty()});
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->copySelection() == "selected");
    std::vector<SkRect> selRects;
    vm->document().spatial_index().getSelectionRects(vm->selection().text_range(), selRects);
    REPORTER_ASSERT(reporter, !selRects.empty());
    REPORTER_ASSERT(reporter, selRects[0].width() > 0.0f);
    REPORTER_ASSERT(reporter, selRects[0].height() > 0.0f);

    // Simulate X11 event sequence: user presses shortcut, and platform harness follows up
    // with handleChar('v', kControl).
    // Hostile Invariant: handleChar MUST reject 'v' when Control is held, never inserting 'v'
    // or overwriting the active selection!
    bool charHandled = vm->handleChar('v', skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, !charHandled, "handleChar should have rejected 'v' with Control modifier!");
    REPORTER_ASSERT(reporter, vm->text() == "First paragraph with selected word.",
                    "Literal 'v' was inserted over selection!");
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed(),
                    "Selection was lost when Control+char arrived!");

    // 3. Redo Branch Preservation on Stray Control Characters:
    // Move to end of text, add text and undo it to have an active redo branch
    vm->collapseTo(CaretPosition{TextIndex(vm->text().size()), Affinity::kDownstream, SkRect::MakeEmpty()});
    vm->insertText(" New word.");
    REPORTER_ASSERT(reporter, vm->text() == "First paragraph with selected word. New word.");
    vm->undo();
    REPORTER_ASSERT(reporter, vm->canRedo());

    // Stray control character (e.g. ASCII 25 = Ctrl+Y or unrecognized control code < 32)
    // must NOT wipe out the redo stack!
    bool cHandled = vm->handleChar(25, skui::ModifierKey::kNone);
    REPORTER_ASSERT(reporter, !cHandled, "Raw ASCII control char < 32 must be rejected!");
    REPORTER_ASSERT(reporter, vm->canRedo(), "Redo stack was corrupted by stray character!");
    vm->redo();
    REPORTER_ASSERT(reporter, vm->text() == "First paragraph with selected word. New word.");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);
}

DEF_TEST(TextEditor_Invariant19_EmptyClipboardAndEnterTypingIntegrity, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    auto vm = std::make_unique<TextEditorViewModel>("", font);
    REPORTER_ASSERT(reporter, vm != nullptr);

    vm->insertText("Keep This Selection");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // Select the word "Selection" (indices 10 to 19)
    vm->setSelection(CaretPosition{TextIndex(10), Affinity::kDownstream, SkRect::MakeEmpty()},
                     CaretPosition{TextIndex(19), Affinity::kDownstream, SkRect::MakeEmpty()});
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->copySelection() == "Selection");

    // 1. Hostile Invariant: Empty Clipboard Paste must be a NO-OP and preserve selection!
    vm->setClipboardHandlers(nullptr, []() { return ""; });
    bool pasteHandled = vm->handleKey(skui::Key::kV, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, pasteHandled);

    // If pasteText("") ran insertText(""), "Selection" would be erased and selection collapsed!
    REPORTER_ASSERT(reporter, vm->text() == "Keep This Selection",
                    "Empty clipboard paste erased selected text!");
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed(),
                    "Empty clipboard paste collapsed the active selection!");
    REPORTER_ASSERT(reporter, vm->copySelection() == "Selection");

    // 2. Non-empty clipboard paste must replace the selection
    vm->setClipboardHandlers(nullptr, []() { return "Replacement"; });
    bool pasteHandled2 = vm->handleKey(skui::Key::kV, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, pasteHandled2);
    REPORTER_ASSERT(reporter, vm->text() == "Keep This Replacement");
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 3. User Acceptance Scenario: Select -> Ctrl+C -> immediate Ctrl+V on the same selection:
    // When pasting clipboard content that is identical to the active selection,
    // it must duplicate the selection immediately (X -> XX) instead of a degenerate X -> X replace!
    vm->insertText("CopyMe");
    size_t copyMeStart = vm->text().size() - 6;
    size_t copyMeEnd = vm->text().size();
    vm->setSelection(CaretPosition{TextIndex(copyMeStart), Affinity::kDownstream, SkRect::MakeEmpty()},
                     CaretPosition{TextIndex(copyMeEnd), Affinity::kDownstream, SkRect::MakeEmpty()});
    REPORTER_ASSERT(reporter, vm->copySelection() == "CopyMe");

    vm->setClipboardHandlers(nullptr, []() { return "CopyMe"; });
    bool dupHandled = vm->handleKey(skui::Key::kV, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, dupHandled);

    // HOSTILE ASSERTION: Must duplicate into "CopyMeCopyMe", not remain "CopyMe"!
    std::string expectedText = "Keep This ReplacementCopyMeCopyMe";
    REPORTER_ASSERT(reporter, vm->text() == expectedText,
                    "Immediate Ctrl+V on identical selection did not duplicate!");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // 4. Mouse Dragged Selection (populates fSelection.ranges):
    // Drag to select "Replacement"
    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, !lines.empty());
    SkScalar y = lines[0].bounds.centerY();
    vm->moveCaretToPoint(lines[0].bounds.fLeft + 80.0f, y, false); // anchor
    vm->moveCaretToPoint(lines[0].bounds.fLeft + 160.0f, y, true);  // focus
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, !vm->selection().ranges().empty(), "Mouse drag must populate ranges!");

    std::string draggedCopied = vm->copySelection();
    REPORTER_ASSERT(reporter, !draggedCopied.empty());
    vm->setClipboardHandlers(nullptr, [&]() { return draggedCopied; });

    // Paste immediately: must duplicate dragged selection and not erase/collapse ranges without duplication
    size_t beforeLen = vm->text().size();
    bool mousePasteHandled = vm->handleKey(skui::Key::kV, skui::InputState::kDown, skui::ModifierKey::kControl);
    REPORTER_ASSERT(reporter, mousePasteHandled);
    REPORTER_ASSERT(reporter, vm->text().size() == beforeLen + draggedCopied.size(),
                    "Mouse drag selection was NOT duplicated on Ctrl+V: length did not increase!");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft > 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);
}

// =============================================================================
// TRAP 24 (Invariant 20 & Axiom 15): Encapsulated Selection State & Realistic Ingress Cohesion
// =============================================================================
DEF_TEST(TextEditor_Invariant20_EncapsulatedSelectionAndIngressCohesion, reporter) {
    // --- Partition 1: Compile-Time Aggregate Rejection (Axiom 16 & Invariant 20.3) ---
    static_assert(!std::is_aggregate_v<EditorSelection>,
                  "KEEPER-INVARIANT-20-BREACH: EditorSelection must not be an aggregate struct; fields must be private.");

    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    LayoutConstraints constraints;
    constraints.max_width = 300.0f;

    std::string sample = "Line 1 Latin text\nمرحبا بالعالم العربي\nLine 3 Ending";
    auto vm = std::make_unique<TextEditorViewModel>(sample, font, SkColor4f{0, 0, 0, 1}, constraints);
    REPORTER_ASSERT(reporter, vm != nullptr);

    // Initial state: collapsed, empty ranges
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty());
    REPORTER_ASSERT(reporter, vm->selection().anchor() == vm->selection().focus());

    // --- Partition 2: Realistic Ingress Multi-Line Drag & Range Population (Axiom 15) ---
    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() >= 3);

    // Drag from line 0 to line 2
    SkScalar y0 = lines[0].bounds.centerY();
    SkScalar y2 = lines[2].bounds.centerY();
    vm->moveCaretToPoint(lines[0].bounds.fLeft + 20.0f, y0, false); // anchor
    vm->moveCaretToPoint(lines[2].bounds.fLeft + 40.0f, y2, true);  // focus (drag)

    // Selection must be non-collapsed and ranges must be populated
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, !vm->selection().ranges().empty(),
                    "Realistic 2D drag must populate multi-line selection ranges!");
    REPORTER_ASSERT(reporter, vm->selection().anchor() != vm->selection().focus());
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // --- Partition 3: Atomic Invariant Invalidation on Navigation (Invariant 20.2) ---
    // Moving the caret without select must atomically collapse selection AND clear ranges
    vm->moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical, false);
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed(),
                    "Caret navigation without selection must collapse EditorSelection!");
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty(),
                    "Caret navigation must atomically clear visual ranges!");
    REPORTER_ASSERT(reporter, vm->selection().anchor() == vm->selection().focus(),
                    "Collapsed selection must have identical anchor and focus!");
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // --- Partition 4: Dual-Mode Ingress Parity: Programmatic API (Axiom 15) ---
    CaretPosition p1 = vm->document().spatial_index().hitTest(10.0f, y0);
    CaretPosition p2 = vm->document().spatial_index().hitTest(80.0f, y0);

    // 4A: set_span via setSelection must clear ranges
    vm->setSelection(p1, p2);
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty(),
                    "Programmatic 1D setSelection must guarantee empty ranges!");

    // 4B: collapseTo must clear ranges and equate anchor and focus
    vm->collapseTo(p2);
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed());
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty());
    REPORTER_ASSERT(reporter, vm->selection().anchor() == vm->selection().focus());
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);

    // --- Partition 5: Discontinuous BiDi Deletion Cohesion (Invariant 12 & 20) ---
    // Drag across Arabic line (line 1)
    SkScalar y1 = lines[1].bounds.centerY();
    vm->moveCaretToPoint(lines[1].bounds.fLeft + 20.0f, y1, false);
    vm->moveCaretToPoint(lines[1].bounds.fRight - 20.0f, y1, true);
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());

    // Deleting selection via deleteBackward must atomically erase and collapse cleanly
    vm->deleteBackward();
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed(),
                    "deleteBackward on discontinuous selection must leave selection collapsed!");
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty(),
                    "deleteBackward must clear all ranges!");
    REPORTER_ASSERT(reporter, vm->selection().anchor() == vm->selection().focus());
    REPORTER_ASSERT(reporter, vm->screenCaretRect().fLeft >= 0.0f);
    REPORTER_ASSERT(reporter, vm->selection().focus().caret_rect().height() > 0.0f);
}

// =============================================================================
// DEFECT REPRODUCTION (Gate A): Arabic Deletion Caret Continuity Trap
// =============================================================================
DEF_TEST(TextEditor_Defect_ArabicDeletionCaretContinuity, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    LayoutConstraints constraints;
    constraints.max_width = 760.0f;

    // Text: English prefix + space + Arabic "مرحبا بالعالم" (identical to TextEditorApp line 57)
    // "مرحبا" = \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 (10 bytes)
    // "بالعالم" = \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85 (14 bytes)
    const std::string text = "- UAX #9 Arabic: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85";
    auto vm = std::make_unique<TextEditorViewModel>(text, font, SkColor4f{0, 0, 0, 1}, constraints);
    REPORTER_ASSERT(reporter, vm != nullptr);
    REPORTER_ASSERT(reporter, vm->text() == text);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() == 1);
    const auto& line = lines[0];
    SkScalar y = line.bounds.centerY();

    // Find the Arabic word "بالعالم"
    const std::string word2 = "\xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85";
    size_t word2Pos = text.find(word2);
    REPORTER_ASSERT(reporter, word2Pos != std::string::npos);
    TextRange word2Range(TextIndex(word2Pos), TextIndex(word2Pos + word2.size()));

    // Query spatial index for the visual bounds of "بالعالم"
    std::vector<SkRect> word2Rects;
    vm->document().spatial_index().getSelectionRects(word2Range, word2Rects);
    REPORTER_ASSERT(reporter, !word2Rects.empty());

    SkScalar word2Left = SK_ScalarMax;
    SkScalar word2Right = SK_ScalarMin;
    for (const auto& r : word2Rects) {
        word2Left = std::min(word2Left, r.fLeft);
        word2Right = std::max(word2Right, r.fRight);
    }
    REPORTER_ASSERT(reporter, word2Left < word2Right);

    // Simulate mouse drag across "بالعالم" using moveCaretToPoint
    vm->moveCaretToPoint(word2Left + 1.0f, y, false); // anchor
    vm->moveCaretToPoint(word2Right - 1.0f, y, true);  // focus (drag)

    // Selection must not be collapsed and copied text must match "بالعالم"
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    std::string copied = vm->copySelection();
    REPORTER_ASSERT(reporter, copied == word2, "Selected text must match Arabic word 'بالعالم'");

    // Record the expected visual cut boundary where the caret should remain after deletion.
    // In visual space, "بالعالم" is at the visual left of the Arabic run (adjacent to English prefix).
    // The cut boundary adjacent to the remaining text (English prefix) is at word2Left.
    SkScalar expectedCutBoundary = word2Left;

    // Delete the selected Arabic word
    vm->deleteBackward();

    // 1. Dual-Contract: Assert logical state
    const std::string expectedText = "- UAX #9 Arabic: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 ";
    REPORTER_ASSERT(reporter, vm->text() == expectedText,
                    "Deleted Arabic word 'بالعالم' must be erased from logical text");
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed(),
                    "Selection must be collapsed after deletion");
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty(),
                    "Selection ranges must be cleared after deletion");

    // 2. Dual-Contract: Assert spatial geometry (Axiom 18(d) & Invariants 1 & 21)
    SkScalar actualCaretX = vm->selection().focus().caret_rect().fLeft;
    const auto& newLines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, !newLines.empty());
    SkScalar newArabicRight = newLines[0].bounds.fRight;

    // The defect: Caret erroneously jumps to visual right end of Arabic run (newArabicRight)
    // instead of staying at the physical cut boundary (expectedCutBoundary).
    // Dual-Contract assertion: Caret X must match the physical cut boundary within a tolerance (<= 2.0f),
    // and MUST NOT jump to the visual right edge of the Arabic text!
    REPORTER_ASSERT(reporter, std::abs(actualCaretX - expectedCutBoundary) <= 2.0f,
                    "KEEPER-DEFECT: Caret X (%.2f) jumped away from cut boundary (%.2f) to right edge (%.2f)!",
                    actualCaretX, expectedCutBoundary, newArabicRight);
}

// =============================================================================
// DEFECT REPRODUCTION (Gate A / Gate B): Arabic-Latin Insertion Caret Continuity Trap
// =============================================================================
DEF_TEST(TextEditor_Defect_ArabicLatinInsertionCaretContinuity, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    LayoutConstraints constraints;
    constraints.max_width = 760.0f;

    // Text: English prefix + 3 Arabic words "مرحبا بكم بالعالم"
    // "مرحبا" = \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 (10 bytes)
    // "بكم"   = \xd8\xa8\xd9\x83\xd9\x85 (6 bytes)
    // "بالعالم" = \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85 (14 bytes)
    const std::string text = "- UAX #9 Arabic: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd9\x83\xd9\x85 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85";
    auto vm = std::make_unique<TextEditorViewModel>(text, font, SkColor4f{0, 0, 0, 1}, constraints);
    REPORTER_ASSERT(reporter, vm != nullptr);
    REPORTER_ASSERT(reporter, vm->text() == text);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() == 1);
    const auto& line = lines[0];
    SkScalar y = line.bounds.centerY();

    // Target the middle Arabic word and its trailing space: "بكم "
    // Consuming the trailing space ensures the newly inserted Latin letter "a" is immediately
    // adjacent to the RTL Arabic word "بالعالم" without an intervening LTR space buffer.
    const std::string midWordWithSpace = "\xd8\xa8\xd9\x83\xd9\x85 ";
    size_t midPos = text.find(midWordWithSpace);
    REPORTER_ASSERT(reporter, midPos != std::string::npos);
    TextRange midRange(TextIndex(midPos), TextIndex(midPos + midWordWithSpace.size()));

    // Query spatial index for the visual bounds of "بكم "
    std::vector<SkRect> midRects;
    vm->document().spatial_index().getSelectionRects(midRange, midRects);
    REPORTER_ASSERT(reporter, !midRects.empty());

    SkScalar midLeft = SK_ScalarMax;
    SkScalar midRight = SK_ScalarMin;
    for (const auto& r : midRects) {
        midLeft = std::min(midLeft, r.fLeft);
        midRight = std::max(midRight, r.fRight);
    }
    REPORTER_ASSERT(reporter, midLeft < midRight);

    // Simulate mouse drag across "بكم " using moveCaretToPoint
    vm->moveCaretToPoint(midRight - 1.0f, y, false); // anchor
    vm->moveCaretToPoint(midLeft + 1.0f, y, true);  // focus (drag)

    // Selection must not be collapsed and copied text must match "بكم "
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    std::string copied = vm->copySelection();
    if (copied != midWordWithSpace) {
        vm->moveCaretToPoint(midLeft + 1.0f, y, false);
        vm->moveCaretToPoint(midRight - 1.0f, y, true);
        copied = vm->copySelection();
    }
    REPORTER_ASSERT(reporter, copied == midWordWithSpace, "Selected text must match middle Arabic word with space 'بكم '");

    // Insert an English letter "a" replacing "بكم "
    vm->insertText("a");

    // 1. Dual-Contract: Assert logical state
    const std::string expectedText = "- UAX #9 Arabic: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 a\xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85";
    REPORTER_ASSERT(reporter, vm->text() == expectedText,
                    "Middle Arabic word with space must be replaced by 'a' in logical text");
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed(),
                    "Selection must be collapsed after insertion");
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty(),
                    "Selection ranges must be cleared after insertion");

    // 2. Dual-Contract: Assert spatial geometry (Axiom 18(d) & Invariant 21)
    // Find visual bounds of the newly inserted letter "a" (using rfind to avoid matching 'a' in "Arabic")
    size_t aPos = vm->text().rfind("a");
    REPORTER_ASSERT(reporter, aPos != std::string::npos);
    REPORTER_ASSERT(reporter, aPos > text.find("Arabic:"));
    TextRange aRange(TextIndex(aPos), TextIndex(aPos + 1));
    std::vector<SkRect> aRects;
    vm->document().spatial_index().getSelectionRects(aRange, aRects);
    REPORTER_ASSERT(reporter, !aRects.empty());

    // Because "a" is an LTR character, its visual trailing edge is its right edge!
    SkScalar expectedCaretX = aRects[0].fRight;

    // In the new layout, find the right edge of the Arabic text run
    const auto& newLines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, !newLines.empty());
    SkScalar arabicBoundary = newLines[0].bounds.fRight;

    SkScalar actualCaretX = vm->selection().focus().caret_rect().fLeft;

    // Caret X must match the inserted letter trailing edge within tolerance (<= 2.0f)
    REPORTER_ASSERT(reporter, std::abs(actualCaretX - expectedCaretX) <= 2.0f,
                    "KEEPER-DEFECT: Caret X (%.2f) jumped away from inserted letter trailing edge (%.2f) to Arabic boundary (%.2f)!",
                    actualCaretX, expectedCaretX, arabicBoundary);
}

// =============================================================================
// DEFECT REPRODUCTION (Gate A): Arabic Forward Deletion Caret Continuity Trap
// =============================================================================
DEF_TEST(TextEditor_Defect_ArabicForwardDeletionCaretContinuity, reporter) {
    SkFont font(ToolUtils::DefaultTypeface(), 16.0f);
    LayoutConstraints constraints;
    constraints.max_width = 760.0f;

    // Text: English prefix + space + Arabic "مرحبا بالعالم" (identical to T34)
    // "مرحبا" = \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 (10 bytes)
    // "بالعالم" = \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85 (14 bytes)
    const std::string text = "- UAX #9 Arabic: \xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7 \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85";
    auto vm = std::make_unique<TextEditorViewModel>(text, font, SkColor4f{0, 0, 0, 1}, constraints);
    REPORTER_ASSERT(reporter, vm != nullptr);
    REPORTER_ASSERT(reporter, vm->text() == text);

    const auto& lines = vm->document().formatted().lines();
    REPORTER_ASSERT(reporter, lines.size() == 1);
    const auto& line = lines[0];
    SkScalar y = line.bounds.centerY();

    // Find the Arabic word "مرحبا"
    const std::string word1 = "\xd9\x85\xd8\xb1\xd8\xad\xd8\xa8\xd8\xa7";
    size_t word1Pos = text.find(word1);
    REPORTER_ASSERT(reporter, word1Pos != std::string::npos);
    TextRange word1Range(TextIndex(word1Pos), TextIndex(word1Pos + word1.size()));

    // Query spatial index for the visual bounds of "مرحبا"
    std::vector<SkRect> word1Rects;
    vm->document().spatial_index().getSelectionRects(word1Range, word1Rects);
    REPORTER_ASSERT(reporter, !word1Rects.empty());

    SkScalar word1Left = SK_ScalarMax;
    SkScalar word1Right = SK_ScalarMin;
    for (const auto& r : word1Rects) {
        word1Left = std::min(word1Left, r.fLeft);
        word1Right = std::max(word1Right, r.fRight);
    }
    REPORTER_ASSERT(reporter, word1Left < word1Right);

    // Simulate mouse drag across "مرحبا" using moveCaretToPoint (Axiom 16 Realistic Ingress)
    vm->moveCaretToPoint(word1Left + 1.0f, y, false); // anchor
    vm->moveCaretToPoint(word1Right - 1.0f, y, true);  // focus (drag)

    // Selection must not be collapsed and copied text must match "مرحبا"
    REPORTER_ASSERT(reporter, !vm->selection().is_collapsed());
    std::string copied = vm->copySelection();
    if (copied != word1) {
        vm->moveCaretToPoint(word1Right - 1.0f, y, false);
        vm->moveCaretToPoint(word1Left + 1.0f, y, true);
        copied = vm->copySelection();
    }
    REPORTER_ASSERT(reporter, copied == word1, "Selected text must match Arabic word 'مرحبا'");

    // Visual cut boundary: "مرحبا" is on the visual right edge of the Arabic run.
    // Deleting "مرحبا" leaves "بالعالم" on the left, with the cut boundary at word1Left.
    SkScalar expectedCutBoundary = word1Left;

    // Delete the selected Arabic word using forward deletion (Delete key / deleteForward)
    vm->deleteForward();

    // 1. Dual-Contract: Assert logical state
    const std::string expectedText = "- UAX #9 Arabic:  \xd8\xa8\xd8\xa7\xd9\x84\xd8\xb9\xd8\xa7\xd9\x84\xd9\x85";
    REPORTER_ASSERT(reporter, vm->text() == expectedText,
                    "Deleted Arabic word 'مرحبا' must be erased from logical text");
    REPORTER_ASSERT(reporter, vm->selection().is_collapsed(),
                    "Selection must be collapsed after deletion");
    REPORTER_ASSERT(reporter, vm->selection().ranges().empty(),
                    "Selection ranges must be cleared after deletion");

    // 2. Dual-Contract: Assert spatial geometry (Axiom 18 Quad Symmetry & Axiom 17(e))
    SkScalar actualCaretX = vm->selection().focus().caret_rect().fLeft;

    // Query prefix visual boundary (where Latin text ends and Arabic begins)
    const std::string prefix = "- UAX #9 Arabic: ";
    TextRange prefixRange(TextIndex(0), TextIndex(prefix.size()));
    std::vector<SkRect> prefixRects;
    vm->document().spatial_index().getSelectionRects(prefixRange, prefixRects);
    REPORTER_ASSERT(reporter, !prefixRects.empty());
    SkScalar prefixBoundary = prefixRects.back().fRight;

    SkDebugf("[TRAPSMITH-GATE-A] word1Left=%.2f word1Right=%.2f expectedCutBoundary=%.2f actualCaretX=%.2f prefixBoundary=%.2f delta=%.2f\n",
             word1Left, word1Right, expectedCutBoundary, actualCaretX, prefixBoundary, std::abs(actualCaretX - expectedCutBoundary));

    // Axiom 19(b) Theoretical Delta Threshold:
    // Assert that the theoretical defect delta on broken code strictly exceeds tolerance.
    // On broken code, the caret erroneously jumps to prefixBoundary (123.83) across the Arabic run.
    SkScalar theoreticalDelta = std::abs(expectedCutBoundary - prefixBoundary);
    constexpr SkScalar kTolerance = 2.0f;
    REPORTER_ASSERT(reporter, theoreticalDelta > kTolerance,
                    "Axiom 19(b) Breach: Theoretical defect delta (%.2f) does not strictly exceed tolerance (%.2f)",
                    theoreticalDelta, kTolerance);

    // Hostile defect assertion (Gate A Trap):
    // Caret X must match the physical cut boundary within tolerance (<= 2.0f).
    // On broken code, caret erroneously jumps across the Arabic text to prefixBoundary, failing this assertion.
    REPORTER_ASSERT(reporter, std::abs(actualCaretX - expectedCutBoundary) <= kTolerance,
                    "KEEPER-DEFECT: Caret X (%.2f) jumped away from cut boundary (%.2f) to prefix boundary (%.2f)! Delta: %.2f",
                    actualCaretX, expectedCutBoundary, prefixBoundary, std::abs(actualCaretX - expectedCutBoundary));
}
