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
   `ParagraphSpatialIndex::moveCaret` and `TextEditorController::moveCaret` must accept `NavigationMode`:
   - `kScreenPhysical`: Arrow keys move visually on screen (Right = visual right, Left = visual left).
   - `kTextLogical`: Arrow keys advance along reading/buffer order (Right = `text_index + 1`, which moves visually leftward inside RTL Arabic/Hebrew runs).
2. **Vertical Caret Navigation (Arrow Up / Down)**:
   Navigating Up or Down must query the line box immediately above or below the current line, evaluating `hitTest(currentX, targetLine.bounds.centerY())`.
3. **Newline Cursor Exclusion**:
   `ParagraphSpatialIndex::hitTest` must place the caret *before* a trailing newline codepoint (`\n`), never after it on the same line. The end of a line terminated by a hard break belongs to the last visible glyph boundary.

---

## Domain Invariant 6: Headless Event Dispatch & Modifier Shielding

1. **The Anti-Glue Law for Input Handling**:
   All keyboard and character handling logic (`handleKey`, `handleChar`) must live inside `TextEditorController`, decoupled from OS window glue (`sk_app::Window`).
2. **Modifier Shielding (Ctrl/Cmd Shortcut Isolation)**:
   When a shortcut key is dispatched (e.g. `Ctrl+A` / `Cmd+A`), the controller must consume the event and shield the text buffer from subsequent character inputs (`onChar('a')`), preventing accidental buffer overwrites.

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
