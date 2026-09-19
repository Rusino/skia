# Text Editor Domain Codex (Tier 2 Local Domain Invariants)
## Authority: Project KEEPER Two-Tier Invariant Hierarchy

This document defines the binding domain-specific invariants for the 4-layer Skia Text Editor engine located in `tools/text_editor`. In accordance with **Section 1.5 of the KEEPER Master Constitution** (`KEEPER_INSTRUCTIONS_FOR_AGENTS.md`), these domain rules carry the exact same binding authority as Tier 1 master invariants. Any AI agent or developer modifying `tools/text_editor` must strictly satisfy every rule herein.

---

## Domain Invariant 1: Dual-Contract Typography (Logical State + Spatial Caret Geometry)

1. **State + Geometry Co-Verification**:
   Any test that asserts text mutation, insertion, deletion, or cursor navigation must assert BOTH:
   - The logical state: exact UTF-8 `text()` content and `selection().focus.text_index`.
   - The spatial geometry: `caret_rect` bounds must be non-empty, with `height > 0`, valid baseline bounds, and non-decreasing coordinates across directional steps.
2. **Caret Position Retention Across Deletions**:
   Executing `deleteBackward()` (Backspace) or `deleteForward()` (Delete) must preserve the active visual cursor position corresponding to the remaining character boundary. Under no circumstances may a deletion operation reset `caret_rect.fLeft` to `0.0f` or line origin.
3. **Empty Text Metric Fallback**:
   When the document buffer is empty (`text().empty()`), the line box array is empty. The engine must compute a valid fallback `caret_rect` from the font metrics (`SkFontMetrics::fAscent` and `fDescent`) of the default font rather than collapsing to an invisible zero-height rectangle.

---

## Domain Invariant 2: Multi-Script Font Fallback & The No-Tofu Law

1. **Zero Silent Degradation (`.notdef` Tofu Prohibition)**:
   The rendering engine must never draw glyph ID 0 (`.notdef` tofu / empty box) for any printable Unicode character in any script supported by installed system fonts.
2. **Dynamic Character-Level Font Fallback**:
   Layer 1 (`UnicodeParagraph`) must not assume a single static font for an entire paragraph or style span. If the style font lacks glyph coverage for a codepoint (`font.unicharToGlyph(u) == 0`), Layer 1 must automatically query `SkFontMgr::matchFamilyStyleCharacter` to partition the run and resolve a script-capable fallback font (e.g. `DejaVu Sans`, `Noto Sans Arabic`, CJK fonts).
3. **Control Code Zero-Width Filtering**:
   Zero-width control characters (`
`, ``, `	`, formatting controls `U+200B`–`U+200F`) must have `is_zero_width_control = true` and MUST BE FILTERED OUT in `TextEditorPainter` prior to invoking `canvas->drawGlyphs`. Control codes must never produce visual tofu boxes.
4. **Exhaustive Multi-Script Matrix Requirement**:
   The automated test suite in `TextEditorTest.cpp` must maintain an exhaustive script matrix trap verifying that Latin, Arabic (RTL), Hebrew (RTL), Cyrillic, Greek, and CJK ideographs shape valid, non-zero glyphs without tofu.

---

## Domain Invariant 3: Line Breaking & Paragraph Formatting (UAX #14)

1. **Word-Boundary Breaking Law**:
   Line layout in `FormattedParagraph` must respect Unicode Line Breaking Algorithm (UAX #14) opportunities. When wrapping text under horizontal width constraints, lines must break at word / whitespace boundaries. Cluster-level splitting is strictly prohibited unless an individual word exceeds the total available line width on an empty line.
2. **Dynamic Zalgo Height Expansion**:
   `LineBox` visual bounds must dynamically expand vertically to encompass stacked diacritics and combining marks (Zalgo text). Marks with negative vertical offsets must expand `line.ascent`, and marks with positive vertical offsets must expand `line.descent`. Bounding boxes must never clip stacked marks.
3. **Terminal Ellipsis Termination**:
   When lines exceed `max_lines` or horizontal bounds with an ellipsis constraint, truncation must replace trailing clusters with the terminal ellipsis glyph (`…`) without exceeding `max_width`.

---

## Domain Invariant 4: Single-Pass Shaping & 1-to-1 Mapping (HarfBuzz liga=0, ccmp=0)

1. **Preservation of 1-to-1 Codepoint-to-Glyph Mapping**:
   In Layer 2 (`ShapedParagraph`), HarfBuzz must execute in single-pass mode with standard ligatures and composition explicitly disabled (`liga=0`, `ccmp=0`, `dlig=0`, `calt=0`).
2. **Glyph Independence Invariant**:
   For editable text, shaping retains distinct glyph entries for base and combining marks ('e' + `́`) to ensure accurate per-glyph metrics. Downstream navigational unification is governed by Domain Invariant 7.

---

## Domain Invariant 5: BiDi Directional Navigation & Caret Hit-Testing

1. **Dual Navigation Modes**:
   `ParagraphSpatialIndex::moveCaret` and `TextEditorViewModel::moveCaret` must accept `NavigationMode`:
   - `kScreenPhysical`: Arrow keys move visually on screen (Right = visual right, Left = visual left).
   - `kTextLogical`: Arrow keys advance along reading/buffer order (Right = `text_index + 1`, which moves visually leftward inside RTL Arabic/Hebrew runs).
2. **Vertical Caret Navigation (Arrow Up / Down)**:
   Navigating Up or Down must query the line box immediately above or below the current line, evaluating `hitTest(currentX, targetLine.bounds.centerY())`.
3. **Newline Cursor Exclusion**:
   `ParagraphSpatialIndex::hitTest` must place the caret *before* a trailing newline codepoint (`\n`), never after it on the same line. The end of a line terminated by a hard break belongs to the last visible glyph boundary.

---

## Domain Invariant 6: Headless Event Dispatch & Modifier Shielding

1. **The Anti-Glue Law for Input Handling**:
   All keyboard and character handling logic (`handleKey`, `handleChar`) must live inside `TextEditorViewModel`, decoupled from OS window glue (`sk_app::Window`).
2. **Modifier Shielding (Ctrl/Cmd Shortcut Isolation)**:
   When a shortcut key is dispatched (e.g. `Ctrl+A` / `Cmd+A`), the ViewModel must consume the event and shield the text buffer from subsequent character inputs (`onChar('a')`), preventing accidental buffer overwrites.

---

## Domain Invariant 7: Extended Grapheme Cluster Atomic Navigation & Spatial Indexing

1. **Atomic Grapheme Cluster Unification**:
   Zero-width combining marks (e.g. Zalgo stacked diacritics, Arabic harakat/tashkeel, Hebrew niqqud, combining accents) and sub-grapheme glyphs must NEVER produce standalone, zero-width `ClusterBox`es in Layer 4 (`ParagraphSpatialIndex`). All combining marks must merge into the preceding base `ClusterBox`, extending its `text_range.end`, expanding its vertical `bounds` for stacked marks, and joining `glyph_range`.
2. **Single-Keystroke Navigation**:
   Directional navigation (Left/Right arrow keys) must step across the entire extended grapheme cluster in a single keypress. Caret movement must never require multiple keystrokes that leave the caret visually frozen at intermediate zero-width diacritics.
3. **Hit-Testing Affinity & Internal Snapping**:
   - Hit-testing on a base character with attached combining marks must resolve to either the start of the cluster (left half) or past the entire cluster (right half), never landing at an internal zero-width mark boundary.
   - If the caret index is positioned inside a multi-codepoint grapheme cluster (e.g. via programmatic positioning or mutation), backward navigation (`kLeft`) must snap to the start of that cluster.

---

## Domain Invariant 8: Cross-Layer Stress Corpus & Zero-Delta Phantom Navigation Law

1. **Centralized Stress Corpus Requirement**:
   Stress test inputs (Zalgo stacked marks, Arabic vowels/tashkeel, Hebrew niqqud, multi-line mixed linebreaks, empty buffers) must reside in a centralized header (`StressCorpus.h`). All functional layers (Formatting, Spatial Navigation, Mutation) must systematically execute against this shared corpus to prevent partial-dimension testing blindspots.
2. **Zero-Delta Phantom Navigation Prohibition**:
   Walking the caret across any non-empty text buffer must NEVER produce a zero-advance step where logical index advances while spatial position ($X, Y$) remains frozen. Every directional navigation step must produce non-zero visual motion or reach an authentic line/document boundary.

---

## Domain Invariant 9: Model-View-ViewModel (MVVM) Decoupling & Zero-Allocation Streaming Rendering

1. **Model Domain Purity & Monotonic Revision**:
   - `TextDocument` strictly encapsulates domain text, style spans, layout constraints, and the 4 immutable layout layers (`UnicodeParagraph`, `ShapedParagraph`, `FormattedParagraph`, `ParagraphSpatialIndex`).
   - `TextDocument` contains zero knowledge of carets, selections, scroll offsets, blink timers, or windowing/rendering concepts.
   - Every mutating operation (`insert`, `erase`, `replace`, `setStyles`, `setConstraints`) strictly increments `revision()` monotonically.
2. **Atomic Replacement & Style Span Range Continuity**:
   - Replacing text over an active non-collapsed selection must execute atomically via `replace(range, text)` with a single layout pipeline rebuild and single revision increment.
   - Any mutation automatically shifts and truncates overlapping/subsequent `StyleSpan` ranges to prevent style drift.
3. **Headless ViewModel Session Management**:
   - `TextEditorViewModel` encapsulates interaction session state (selection, caret blinking, viewport scroll offset).
   - All navigation and hit-testing queries on the hot path must execute without heap allocations ($O(1)$ allocations).
   - High-level editing actions automatically synchronize caret position with the updated spatial index and notify the view via `RedrawCallback`.
4. **Zero-Allocation Streaming Visitor Pipeline**:
   - Rendering data is delivered via a coordinate-explicit streaming visitor hierarchy:
     `FormattedParagraph::visitParagraphRuns(localClip, visitor)` $\to$
     `TextDocument::visitDocumentRuns(docClip, visitor)` $\to$
     `TextEditorViewModel::visitScreenRuns(screenClip, visitor)`.
   - The visitor delivers pre-baked `SkSpan<const SkGlyphID>` and `SkSpan<const SkPoint>` directly to `SkCanvas::drawGlyphs`. Frame rendering must perform zero heap allocations in the draw loop.

---

## Domain Invariant 10: Typographic Caret Bounds vs. Ink Bounds Separation

1. **Typographic Height Invariant**:
   - The vertical height and position of the caret must be strictly computed from the active typographic font metrics ($Ascent + Descent$) and paragraph line spacing:
     $$\text{CaretTop} = \text{line.baseline} + \text{line.ascent}, \quad \text{CaretHeight} = |\text{line.ascent}| + |\text{line.descent}|$$
   - The caret bounds MUST NEVER expand to encompass stacked diacritics, combining marks, or Zalgo text.
2. **Ink Bounds Isolation**:
   - Dynamic diacritic expansion (`maxZalgoTop`, `maxZalgoBottom`) belongs strictly to `line.bounds` for redraw invalidation and visual clipping. It must never leak into caret geometry. For any single-style line, $\text{caret.height}() \le \text{font.getSize}() \times 1.35$.

---

## Domain Invariant 11: BiDi Cluster Hit-Testing & Selection Geometry

1. **BiDi Affinity Invariant**:
   - In Layer 4 (`ParagraphSpatialIndex`), hit-testing inside an RTL cluster (`ClusterBox::is_rtl == true`) must invert logical boundary assignment:
     - The visually left half of an RTL cluster corresponds to `cb.text_range.end` (logical end / upstream boundary);
     - The visually right half of an RTL cluster corresponds to `cb.text_range.start` (logical start / downstream boundary).
2. **Physical Drag Monotonicity**:
   - When a mouse drag gesture moves monotonically along the X-axis across text runs (whether LTR or RTL), the visual selection bounds must monotonically track the physical pointer position. Hit-testing must not collapse or mirror selection spans when entering RTL runs.

---

## Domain Invariant 12: Continuous Physical Selection & Discontinuous Logical Deletion Across BiDi Boundaries

1. **Physical Selection Bounds Dominance**:
   - During interactive mouse drag across mixed LTR/RTL text runs, visual selection must strictly illuminate the physical horizontal interval $[X_{anchor}, X_{focus}]$ on the active line.
   - Crossing a directionality boundary (e.g. from an LTR space into the visually adjacent end of an RTL run) must NEVER cause the selection to prematurely expand across the unselected portions of the RTL run or invert unselected characters.
2. **Discontinuous Logical Mapping**:
   - The selected text region spanning cross-directional runs must be represented as a canonical set of non-overlapping, sorted logical ranges ($\{R_1, R_2, \dots, R_k\}$).
   - Only the glyph clusters physically intersecting $[X_{anchor}, X_{focus}]$ contribute their logical byte spans to the selection.
3. **Inverted Topological Deletion Order**:
   - Deletion of a discontinuous selection (via Backspace, Delete, or character replacement) must execute against the underlying document in reverse logical order (from highest byte offset down to lowest byte offset).
   - Deletion must remove strictly and exclusively the bytes corresponding to the physically highlighted glyphs, leaving unselected logical segments of adjacent runs structurally intact.

## Domain Invariant 13: Single-Source Render Projection & Painter Purity

1. **Passive Projection Consumer Invariant**:
   - `TextEditorPainter` is strictly a passive consumer of `TextEditorViewModel` projections. It is strictly prohibited from recalculating geometry, querying `ParagraphSpatialIndex`, or constructing `TextRange` spans internally.
   - All visual elements (selection rectangles, caret bounds) must be queried directly from `viewModel.screenSelectionRects()` and `viewModel.screenCaretRect()`.
2. **Dual-Contract Canvas Testing Requirement**:
   - Every regression trap or interactive test that validates cursor movement or text selection must assert BOTH the ViewModel geometry (`screenSelectionRects()`) AND the actual rendered primitives produced by passing a headless/mock canvas to `TextEditorPainter::Paint()`.

---

## Domain Invariant 14: Control Character Ingestion, Line Splitting & Immediate Soft-Tab Normalization

1. **Document Structure Control Separation**:
   - The Enter / Return keyboard event (`skui::Key::kOK` or `skui::Key::kReturn`) is a structural document command. It must split lines/paragraphs atomically by invoking `insertText("\n")`.
   - Newline sequences (`\r\n` or single `\r`) from external ingest must be normalized to `\n` before reaching downstream layers or shaping engines.
2. **Immediate Soft-Tab Normalization**:
   - The Tab keyboard event (`skui::Key::kTab`) must not inject raw `0x09` bytes into the font shaping engine.
   - Tab must immediately resolve to $N$ soft-space characters (`0x20`), where $N = \text{tabSize} - (\text{column} \pmod{\text{tabSize}})$, with $\text{tabSize} = 4$.
   - Any raw `\t` encountered in external text ingest must likewise be normalized into appropriate soft spaces.
3. **Control Character Noise Elimination & Shaping Control Preservation**:
   - Destructive or noisy control codes (ASCII `0x00–0x1F` other than `\n`, `0x7F` DEL, C1 controls) must be sanitized and rejected upon ingest.
   - Unicode Format Controls (`Cf` category, including ZWJ `U+200D`, ZWNJ `U+200C`, LRM `U+200E`, RLM `U+200F`) are strictly preserved as essential typographical inputs for HarfBuzz and BiDi analysis.
