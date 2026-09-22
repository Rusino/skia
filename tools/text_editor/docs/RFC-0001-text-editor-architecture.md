# RFC-0001: 4-Layer Immutable Text Editor & Diacritic Sub-Editor
**Status:** Approved by The Overgod  
**Standard:** C++20  
**Location:** `skia/tools/text_editor`  
**Author:** Project KEEPER (The Invariant Inquisitor & The Architect)

---

## 1. System Invariants (The Inquisitor's Seal)

1. **Paragraph-Level Immutability**:
   - Each layer object (`UnicodeParagraph`, `ShapedParagraph`, `FormattedParagraph`, `ParagraphSpatialIndex`) is fully immutable once constructed.
   - Zero-cost const-concurrency: concurrent read-only queries from multiple threads require zero locks, zero atomics, and zero synchronization primitives.
2. **Zero-Heap Query Contract (The Quartermaster Directive)**:
   - Once `ParagraphSpatialIndex` is built, query methods (`hitTest`, `moveVisual`, `moveLogical`) must execute with zero heap allocations on the hot path.
3. **Single-Pass Deterministic Shaping**:
   - Standard typographical ligatures (`liga=0`) and character composition (`ccmp=0`) are suppressed by default.
   - Guarantees 1-to-1 mapping between codepoints and glyphs for text and combining marks.
4. **Intra-Cluster Attachment Model**:
   - Diacritics and combining marks are anchored via OpenType GPOS (`mark` to base, `mkmk` mark to mark).
   - Freestanding diacritic stacks anchor to Unicode Dotted Circle `◌` (`U+25CC`).
   - Zero-width control characters (`CGJ`, `ZWJ`, `ZWNJ`) are materialized as interactive bracketed pins with synthetic visual bounding boxes.
   - Diacritics are strictly prohibited from attaching to emoji base characters.

---

## 2. Layer Topology & Data Contracts

```
[UTF-8 Text + Style Spans]
         │
         ▼
[Layer 1: UnicodeParagraph] ──────────► Outputs: std::vector<ItemizedRun>
         │                              (Script, BidiLevel, Font, TextRange)
         ▼
[Layer 2: ShapedParagraph]  ──────────► Outputs: std::vector<ShapedRun>
         │                              (GlyphIDs, Positions, Offsets, 1-to-1 ClusterMap)
         ▼
[Layer 3: FormattedParagraph] ────────► Outputs: std::vector<LineBox>
         │                              (Line slices, visual BiDi order, dynamic ascent/descent)
         ▼
[Layer 4: ParagraphSpatialIndex] ─────► Precomputes: ClusterBoxes, GlyphBoxes
                                        APIs: hitTest, moveVisual, moveLogical, getSelectionRects
```
