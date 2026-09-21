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
4. **2D Multi-Line Drag Continuity & Intermediate Saturation (Dimensional Honesty)**:
   - When an interactive visual drag spans multiple lines ($(x_1, y_1) \to (x_2, y_2)$ where $\text{line}(y_1) \ne \text{line}(y_2)$):
     - **Downward Drag ($\text{startLine} < \text{endLine}$)**:
       - On `startLine`: All clusters intersecting $[x_1, +\infty)$ (trailing line direction) are selected.
       - On intermediate lines ($k \in (\text{startLine}, \text{endLine})$): 100% of the clusters on the line are saturated/selected across their entire line bounds.
       - On `endLine`: All clusters intersecting $[-\infty, x_2]$ (leading line direction) are selected.
     - **Upward Drag ($\text{startLine} > \text{endLine}$)**:
       - On `endLine` (upper focus): All clusters intersecting $[x_2, +\infty)$ are selected.
       - On intermediate lines ($k \in (\text{endLine}, \text{startLine})$): 100% of clusters are saturated.
       - On `startLine` (lower anchor): All clusters intersecting $[-\infty, x_1]$ are selected.
   - **Dimensional Honesty Invariant**: Under no circumstances may a multi-line visual drag collapse to a single line or silently discard previous lines. `getSelectionForVisualDrag` must defensively assert cross-line continuity in Debug builds.

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

---

## Domain Invariant 15: Soft-Wrap Boundary Disambiguation & Caret Affinity

1. **Soft-Wrap Boundary Singularity**:
   - In soft-wrapped lines, the transition from line $k$ to line $k+1$ occurs without a hard newline character (`\n`). The scalar index $I = \text{line}_k.\text{text\_range.end} = \text{line}_{k+1}.\text{text\_range.start}$ is topologically shared between two distinct geometric screen locations.
2. **Upstream Affinity & Line-Tail Snapping**:
   - If a mouse click or hit-test query lands on line $k$ at or to the right of its content bounds ($\ge \text{line}_k.\text{bounds.fRight}$), the resolved position must take `Affinity::kUpstream`.
   - When resolving screen coordinates for a position with `Affinity::kUpstream` at index $I$, the caret rectangle must be positioned strictly at the right edge of line $k$ ($\text{line}_k.\text{bounds.fRight}$) at vertical coordinate $Y_k$. It must NEVER jump down to the start of line $k+1$.
3. **Downstream Affinity & Line-Head Snapping**:
   - If a hit-test query lands on line $k+1$ at or before its first glyph, the resolved position must take `Affinity::kDownstream`.
   - When resolving screen coordinates for a position with `Affinity::kDownstream` at index $I$, the caret rectangle must be positioned strictly at the left edge of line $k+1$ ($\text{line}_{k+1}.\text{bounds.fLeft}$) at vertical coordinate $Y_{k+1}$.
4. **Step-Through Horizontal Navigation**:
   - Horizontal caret movement across a soft-wrap boundary (via `kRight` or `kLeft`) must transition sequentially through both geometric positions via affinity toggling ($(\text{line}_k.\text{end}, \text{kUpstream}) \leftrightarrow (\text{line}_{k+1}.\text{start}, \text{kDownstream})$) without skipping visual positions on screen.

---

## Domain Invariant 16: Headless Clipboard Interop & Sanitized Insertion

1. **Headless Clipboard Provider Invariant**:
   - `TextEditorViewModel` manages copy, cut, and paste transactions strictly without hard dependencies on OS windowing or display servers.
   - System clipboard interaction is mediated via optional non-allocating or functional hooks (`ClipboardSetter`, `ClipboardGetter`).
2. **Copy Purity & Logical Coherence**:
   - `copySelection()` extracts strictly the logical text corresponding to active selection spans. For discontinuous BiDi selections, text is gathered in canonical logical order.
   - Copying with a collapsed selection is a safe no-op that emits an empty string without altering document or selection state.
3. **Atomic Cut Transaction**:
   - `cutSelection()` extracts the selected text, transfers it to the clipboard provider, and executes an atomic deletion of the selection in a single document rebuild transaction.
4. **Sanitized Paste & Caret Relocation**:
   - `pasteText(std::string_view raw)` atomically replaces active non-collapsed selections.
   - All pasted content is strictly piped through `Invariant 14` input sanitization (normalizing CRLF to `\n`, replacing tabs with soft-spaces, filtering C0 noise and DEL, while preserving UTF-8 shaping controls).
   - Upon completion, the selection must be collapsed, and the caret must be synchronously placed immediately after the inserted text.

---

## Domain Invariant 17: Linear Reversible Command History (Undo/Redo) & Typing Coalescing

1. **Deterministic Bounded Reversibility**:
   - `TextEditorViewModel` maintains a bounded linear history of reversible edit commands (max depth = 1000).
   - Any state mutating document text (`insert`, `delete`, `replace`, `paste`, `cut`) must be executed as a command pair `(ForwardMutation, InverseMutation)` preserving exact byte spans and selection states (`selectionBefore`, `selectionAfter`).
   - Executing `undo()` strictly reverses the mutation and restores `selectionBefore`. Executing `redo()` reapplies the mutation and restores `selectionAfter`.

2. **Typing Coalescing & Boundary Partitioning**:
   - Monotonic character typing is coalesced into a single undoable transaction if and only if:
     (a) Elapsed time between consecutive keystrokes is $\le 750\text{ ms}$;
     (b) Insert position is immediately contiguous;
     (c) The typed character does not cross a word boundary (space, punctuation, newline).
   - Deletions via Backspace/Delete are coalesced similarly within a contiguous run, but never merged with insertions.
   - External paste, cut, block replace, and newline ingestion are atomic and never coalesced.

3. **Branch Truncation & Zero Memory Leak**:
   - Any new mutation executed while the history cursor is before the head of the stack permanently invalidates and truncates all downstream redo commands.
   - Pushing beyond maximum depth drops the oldest command in $O(1)$ time without reallocation churn.

---

## Domain Invariant 18: Modifier Latching, Chained Shortcut Immunity, and Event Absorption

1. **Modifier Latching & Chained Command Integrity**:
   - `TextEditorViewModel` latches the physical state of the `Control` modifier between `Key::kCtrl` down and up events.
   - Chained shortcut execution while holding `Control` (such as sequential `Ctrl+Z`, `Ctrl+Y`, or `Ctrl+C` followed by `Ctrl+V`) must reliably preserve modifier context across all consecutive key events even if the underlying windowing system temporarily drops modifier bits in event state masks.

2. **Accidental Character Insertion & Selection Immunity**:
   - `TextEditorViewModel::handleChar` unconditionally rejects text insertion when `Control` (or `Command`) is active or when raw ASCII control codes ($c < 32$) arrive.
   - Under no circumstances may control shortcut keypresses or modifier chattering insert literal characters (e.g. `'v'`, `'y'`) into the document or overwrite active selections.

3. **Platform Window Event Absorption**:
   - In `TextEditorApp`, any key event successfully consumed by `onKey` as a command shortcut absorbs the subsequent `onChar` event generated within the same event slice by platform window harnesses (e.g. X11), preventing ghost character injection and protecting undo/redo history trees.

---

## Domain Invariant 19: Clipboard Operation Invariants & Selection Duplication

1. **Empty Clipboard Immunity**:
   - Pasting with an empty or whitespace-only/null clipboard must be an absolute NO-OP.
   - It must never delete or overwrite the active selection, nor move the cursor.

2. **Identical Selection Smart Duplication ($X \to XX$)**:
   - When pasting clipboard content that is identical to the active selection (`raw == copySelection()`), the engine must not execute a degenerate replacement ($X \to X$).
   - Instead, it must collapse the selection to the trailing boundary across all active ranges, position the caret adjacent to the selected text, and insert the duplicate adjacent to it ($X \to XX$).

---

## Domain Invariant 20: Encapsulated Selection State & Atomic Range Cohesion

1. **Prohibition of Anemic State Mutations**:
   - `EditorSelection` state transitions (`collapse_to`, `set_span`, `set_ranges`) must be executed exclusively through atomic mutator methods. Direct assignment to individual fields (`anchor`, `focus`, `ranges`) from callers is strictly prohibited.
2. **Range Invariant Preservation**:
   - Any operation collapsing the selection (`collapse_to`) or setting a 1D span (`set_span`) must unconditionally clear `ranges`, guaranteeing that `is_collapsed()` accurately reflects the true state of the selection.
3. **Compile-Time Aggregate Rejection & Private Encapsulation**:
   - `EditorSelection` MUST strictly encapsulate its state members as `private`. Direct access to internal fields from external modules is prohibited; read-only access is mediated exclusively via `const` accessors (`anchor()`, `focus()`, `ranges()`).
   - The header must assert non-aggregate status via `static_assert(!std::is_aggregate_v<EditorSelection>)`.
4. **Postcondition Debug Invariant Assertions**:
   - Every mutator method must defensively assert its postcondition invariants in Debug builds:
     - `collapse_to(pos)` asserts `ranges.empty()` and `anchor == focus`;
     - `set_span(a, f)` asserts `ranges.empty()`;
     - `set_ranges(a, f, r)` asserts `!r.empty() || a == f`.

---

## Domain Invariant 21: BiDi Deletion Caret Continuity & Boundary Retention

1. **Physical Deletion Boundary Retention**:
   - When deleting a selection within or across BiDi (RTL/LTR) runs via `deleteBackward()` or `deleteForward()`, the resulting caret must remain at the exact physical visual cut boundary where the deletion occurred.
2. **Prohibition of RTL Edge Teleportation**:
   - Deletion of an RTL selection must NEVER teleport the caret to `ranges().front().start` if that index corresponds to the visual right edge of the Arabic run or line origin.
   - The caret coordinate $X$ after deletion must be continuous with the remaining text boundary.
3. **BiDi-Aware Caret Geometry Resolution**:
   - `updateCursorPosition` and all caret coordinate resolution methods must query `ParagraphSpatialIndex` or examine cluster directionality (`is_rtl`). In RTL clusters, the logical start boundary of a cluster resides at `bounds.fRight`, and the end boundary at `bounds.fLeft`. Taking `rects[0].fLeft` for RTL text without direction inversion is strictly prohibited.
4. **BiDi Insertion Boundary & Typing Affinity Invariant**:
   - Upon text insertion (`insertText`), the caret MUST maintain `Affinity::kUpstream`, positioning strictly at the trailing edge (for LTR: `bounds.fRight`, for RTL: `bounds.fLeft`) of the newly inserted cluster.
   - It is strictly prohibited to attach the caret to the downstream following cluster or jump to the visual opposite boundary of an adjacent BiDi run.
5. **Prohibition of Synthetic Caret Post-Adjustment**:
   - `insertText` must directly and atomically compute the correct caret geometry via `updateCursorPosition(finalCaret, Affinity::kUpstream)`. Calling synthetic adjustments (such as `moveCaret(..., kRight, ...)`) after insertion is strictly prohibited.
