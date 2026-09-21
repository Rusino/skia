/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/ParagraphSpatialIndex.h"
#include "src/base/SkUTF.h"
#include <algorithm>

namespace skia::text_editor {

namespace {

class ParagraphSpatialIndexImpl : public ParagraphSpatialIndex {
public:
    ParagraphSpatialIndexImpl(std::shared_ptr<const FormattedParagraph> formatted_para)
        : fFormatted(std::move(formatted_para))
    {
        buildIndex();
    }

    const FormattedParagraph& formatted() const override { return *fFormatted; }

    bool isSoftWrapBoundary(size_t lineIdx) const {
        if (!fFormatted || lineIdx + 1 >= fFormatted->lines().size()) {
            return false;
        }
        const auto& lines = fFormatted->lines();
        if (lines[lineIdx].has_hard_break) {
            return false;
        }
        return lines[lineIdx].text_range.end == lines[lineIdx + 1].text_range.start;
    }

    CaretPosition hitTest(SkScalar x, SkScalar y) const override {
        CaretPosition pos;
        if (!fFormatted || fFormatted->lines().empty()) {
            return pos;
        }

        const auto& lines = fFormatted->lines();
        size_t targetLineIdx = 0;

        // 1. Locate line by Y coordinate
        for (size_t i = 0; i < lines.size(); ++i) {
            if (y >= lines[i].bounds.fTop && y <= lines[i].bounds.fBottom) {
                targetLineIdx = i;
                break;
            }
            if (y < lines[i].bounds.fTop) {
                targetLineIdx = i;
                break;
            }
            targetLineIdx = i;
        }

        const auto& line = lines[targetLineIdx];
        pos.caret_rect = SkRect::MakeXYWH(line.bounds.fLeft, line.baseline + line.typographic_ascent, 1.0f,
                                          std::abs(line.typographic_ascent) + std::abs(line.typographic_descent));

        // 2. Locate closest cluster on line by X coordinate
        if (targetLineIdx < fLineClusters.size() && !fLineClusters[targetLineIdx].empty()) {
            const auto& clusters = fLineClusters[targetLineIdx];
            std::string_view fullText = fFormatted->shaped().unicode().text();
            for (const auto& cb : clusters) {
                bool isNewline = (cb.text_range.start.value < fullText.size() &&
                                  fullText[cb.text_range.start.value] == '\n');
                if (isNewline) {
                    pos.text_index = cb.text_range.start;
                    pos.affinity = Affinity::kDownstream;
                    pos.caret_rect.fLeft = cb.bounds.fLeft;
                    pos.caret_rect.fRight = cb.bounds.fLeft + 1.0f;
                    return pos;
                }
                if (x <= cb.bounds.fLeft || x < cb.bounds.centerX()) {
                    // Left half of cluster box:
                    // For LTR: corresponds to start of cluster.
                    // For RTL: corresponds to end of cluster (characters read right-to-left).
                    if (cb.is_rtl) {
                        pos.text_index = cb.text_range.end;
                        pos.affinity = Affinity::kUpstream;
                    } else {
                        pos.text_index = cb.text_range.start;
                        pos.affinity = Affinity::kDownstream;
                    }
                    pos.caret_rect.fLeft = cb.bounds.fLeft;
                    pos.caret_rect.fRight = cb.bounds.fLeft + 1.0f;
                    return pos;
                } else if (x <= cb.bounds.fRight) {
                    // Right half of cluster box:
                    // For LTR: corresponds to end of cluster.
                    // For RTL: corresponds to start of cluster.
                    if (cb.is_rtl) {
                        pos.text_index = cb.text_range.start;
                        pos.affinity = Affinity::kDownstream;
                    } else {
                        pos.text_index = cb.text_range.end;
                        pos.affinity = Affinity::kUpstream;
                    }
                    pos.caret_rect.fLeft = cb.bounds.fRight;
                    pos.caret_rect.fRight = cb.bounds.fRight + 1.0f;
                    return pos;
                }
            }
            // Past right edge of line
            const auto& lastCb = clusters.back();
            bool isNewline = (lastCb.text_range.start.value < fullText.size() &&
                              fullText[lastCb.text_range.start.value] == '\n');
            if (isNewline) {
                pos.text_index = lastCb.text_range.start;
                pos.affinity = Affinity::kDownstream;
                pos.caret_rect.fLeft = lastCb.bounds.fLeft;
                pos.caret_rect.fRight = lastCb.bounds.fLeft + 1.0f;
            } else {
                if (lastCb.is_rtl) {
                    pos.text_index = lastCb.text_range.start;
                    pos.affinity = Affinity::kDownstream;
                } else {
                    pos.text_index = lastCb.text_range.end;
                    pos.affinity = Affinity::kUpstream;
                }
                pos.caret_rect.fLeft = lastCb.bounds.fRight;
                pos.caret_rect.fRight = lastCb.bounds.fRight + 1.0f;
            }
        }

        return pos;
    }

    CaretPosition moveCaret(
        const CaretPosition& current,
        CursorDirection dir,
        MovementGranularity granularity,
        NavigationMode mode) const override
    {
        CaretPosition next = current;
        if (!fFormatted || fFormatted->lines().empty() || fFlatClusters.empty()) {
            return next;
        }

        const auto& lines = fFormatted->lines();

        switch (dir) {
            case CursorDirection::kRight: {
                // If currently at upstream on a soft-wrap boundary, transition to downstream on next line
                if (current.affinity == Affinity::kUpstream) {
                    for (size_t k = 0; k + 1 < lines.size(); ++k) {
                        if (lines[k].text_range.end == current.text_index && isSoftWrapBoundary(k)) {
                            next.text_index = current.text_index;
                            next.affinity = Affinity::kDownstream;
                            const auto& nextLine = lines[k + 1];
                            next.caret_rect = SkRect::MakeXYWH(nextLine.bounds.fLeft,
                                                               nextLine.baseline + nextLine.typographic_ascent,
                                                               1.0f,
                                                               std::abs(nextLine.typographic_ascent) + std::abs(nextLine.typographic_descent));
                            if (k + 1 < fLineClusters.size() && !fLineClusters[k + 1].empty()) {
                                next.caret_rect.fLeft = fLineClusters[k + 1].front().bounds.fLeft;
                                next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                            }
                            return next;
                        }
                    }
                }

                if (mode == NavigationMode::kTextLogical) {
                    size_t logicalIdx = fLogicalClusters.size();
                    for (size_t i = 0; i < fLogicalClusters.size(); ++i) {
                        if (fLogicalClusters[i].text_range.contains(current.text_index)) {
                            logicalIdx = i;
                            break;
                        }
                    }
                    if (logicalIdx < fLogicalClusters.size()) {
                        if (logicalIdx + 1 < fLogicalClusters.size()) {
                            // Check if advancing crosses into the next line across a soft-wrap boundary
                            for (size_t k = 0; k + 1 < lines.size(); ++k) {
                                if (lines[k].text_range.contains(current.text_index) && isSoftWrapBoundary(k) &&
                                    fLogicalClusters[logicalIdx + 1].text_range.start >= lines[k].text_range.end) {
                                    next.text_index = lines[k].text_range.end;
                                    next.affinity = Affinity::kUpstream;
                                    const auto& curLine = lines[k];
                                    next.caret_rect = SkRect::MakeXYWH(curLine.bounds.fRight,
                                                                       curLine.baseline + curLine.typographic_ascent,
                                                                       1.0f,
                                                                       std::abs(curLine.typographic_ascent) + std::abs(curLine.typographic_descent));
                                    if (k < fLineClusters.size() && !fLineClusters[k].empty()) {
                                        next.caret_rect.fLeft = fLineClusters[k].back().bounds.fRight;
                                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                                    }
                                    return next;
                                }
                            }
                            next.text_index = fLogicalClusters[logicalIdx + 1].text_range.start;
                            next.affinity = Affinity::kDownstream;
                            next.caret_rect = fLogicalClusters[logicalIdx + 1].bounds;
                            next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                        } else {
                            next.text_index = fLogicalClusters.back().text_range.end;
                            next.affinity = Affinity::kUpstream;
                            next.caret_rect = fLogicalClusters.back().bounds;
                            next.caret_rect.fLeft = next.caret_rect.fRight;
                            next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                        }
                    }
                } else {
                    size_t currentIdx = fFlatClusters.size();
                    for (size_t i = 0; i < fFlatClusters.size(); ++i) {
                        if (fFlatClusters[i].text_range.contains(current.text_index)) {
                            currentIdx = i;
                            break;
                        }
                    }
                    if (currentIdx < fFlatClusters.size()) {
                        if (currentIdx + 1 < fFlatClusters.size()) {
                            // Check if advancing crosses into the next line across a soft-wrap boundary
                            for (size_t k = 0; k + 1 < lines.size(); ++k) {
                                if (lines[k].text_range.contains(current.text_index) && isSoftWrapBoundary(k) &&
                                    fFlatClusters[currentIdx + 1].text_range.start >= lines[k].text_range.end) {
                                    next.text_index = lines[k].text_range.end;
                                    next.affinity = Affinity::kUpstream;
                                    const auto& curLine = lines[k];
                                    next.caret_rect = SkRect::MakeXYWH(curLine.bounds.fRight,
                                                                       curLine.baseline + curLine.typographic_ascent,
                                                                       1.0f,
                                                                       std::abs(curLine.typographic_ascent) + std::abs(curLine.typographic_descent));
                                    if (k < fLineClusters.size() && !fLineClusters[k].empty()) {
                                        next.caret_rect.fLeft = fLineClusters[k].back().bounds.fRight;
                                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                                    }
                                    return next;
                                }
                            }
                            next.text_index = fFlatClusters[currentIdx + 1].text_range.start;
                            next.affinity = Affinity::kDownstream;
                            next.caret_rect = fFlatClusters[currentIdx + 1].bounds;
                            next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                        } else {
                            next.text_index = fFlatClusters.back().text_range.end;
                            next.affinity = Affinity::kUpstream;
                            next.caret_rect = fFlatClusters.back().bounds;
                            next.caret_rect.fLeft = next.caret_rect.fRight;
                            next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                        }
                    }
                }
                break;
            }
            case CursorDirection::kLeft: {
                // If currently at downstream at the start of a soft-wrapped line, transition to upstream on previous line
                if (current.affinity == Affinity::kDownstream) {
                    for (size_t k = 0; k + 1 < lines.size(); ++k) {
                        if (lines[k + 1].text_range.start == current.text_index && isSoftWrapBoundary(k)) {
                            next.text_index = current.text_index;
                            next.affinity = Affinity::kUpstream;
                            const auto& prevLine = lines[k];
                            next.caret_rect = SkRect::MakeXYWH(prevLine.bounds.fRight,
                                                               prevLine.baseline + prevLine.typographic_ascent,
                                                               1.0f,
                                                               std::abs(prevLine.typographic_ascent) + std::abs(prevLine.typographic_descent));
                            if (k < fLineClusters.size() && !fLineClusters[k].empty()) {
                                next.caret_rect.fLeft = fLineClusters[k].back().bounds.fRight;
                                next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                            }
                            return next;
                        }
                    }
                }

                if (mode == NavigationMode::kTextLogical) {
                    size_t logicalIdx = fLogicalClusters.size();
                    for (size_t i = 0; i < fLogicalClusters.size(); ++i) {
                        if (fLogicalClusters[i].text_range.contains(current.text_index)) {
                            logicalIdx = i;
                            break;
                        }
                    }
                    bool atEndOfLogical = (current.text_index >= fLogicalClusters.back().text_range.end);
                    if (atEndOfLogical) {
                        next.text_index = fLogicalClusters.back().text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fLogicalClusters.back().bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    } else if (logicalIdx < fLogicalClusters.size() && current.text_index > fLogicalClusters[logicalIdx].text_range.start) {
                        next.text_index = fLogicalClusters[logicalIdx].text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fLogicalClusters[logicalIdx].bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    } else if (logicalIdx > 0 && logicalIdx < fLogicalClusters.size()) {
                        next.text_index = fLogicalClusters[logicalIdx - 1].text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fLogicalClusters[logicalIdx - 1].bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    }
                } else {
                    size_t currentIdx = fFlatClusters.size();
                    for (size_t i = 0; i < fFlatClusters.size(); ++i) {
                        if (fFlatClusters[i].text_range.contains(current.text_index)) {
                            currentIdx = i;
                            break;
                        }
                    }
                    bool atEndOfText = (current.text_index >= fFlatClusters.back().text_range.end);
                    if (atEndOfText) {
                        next.text_index = fFlatClusters.back().text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fFlatClusters.back().bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    } else if (currentIdx < fFlatClusters.size() && current.text_index > fFlatClusters[currentIdx].text_range.start) {
                        next.text_index = fFlatClusters[currentIdx].text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fFlatClusters[currentIdx].bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    } else if (currentIdx > 0 && currentIdx < fFlatClusters.size()) {
                        next.text_index = fFlatClusters[currentIdx - 1].text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fFlatClusters[currentIdx - 1].bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    }
                }
                break;
            }
            case CursorDirection::kDown: {
                size_t currentLineIdx = lines.size();
                SkScalar curY = current.caret_rect.centerY();
                if (current.caret_rect.height() <= 0) {
                    curY = current.caret_rect.fTop;
                }
                for (size_t i = 0; i < lines.size(); ++i) {
                    if (curY >= lines[i].bounds.fTop && curY <= lines[i].bounds.fBottom) {
                        currentLineIdx = i;
                        break;
                    }
                }
                if (currentLineIdx >= lines.size()) {
                    for (size_t i = 0; i < fLineClusters.size(); ++i) {
                        if (fLineClusters[i].empty()) continue;
                        if (current.text_index >= fLineClusters[i].front().text_range.start &&
                            current.text_index <= fLineClusters[i].back().text_range.end) {
                            currentLineIdx = i;
                            break;
                        }
                    }
                }
                if (currentLineIdx < lines.size() && currentLineIdx + 1 < lines.size()) {
                    size_t targetLineIdx = currentLineIdx + 1;
                    SkScalar targetY = lines[targetLineIdx].bounds.centerY();
                    SkScalar targetX = current.caret_rect.fLeft;
                    next = hitTest(targetX, targetY);
                }
                break;
            }
            case CursorDirection::kUp: {
                size_t currentLineIdx = lines.size();
                SkScalar curY = current.caret_rect.centerY();
                if (current.caret_rect.height() <= 0) {
                    curY = current.caret_rect.fTop;
                }
                for (size_t i = 0; i < lines.size(); ++i) {
                    if (curY >= lines[i].bounds.fTop && curY <= lines[i].bounds.fBottom) {
                        currentLineIdx = i;
                        break;
                    }
                }
                if (currentLineIdx >= lines.size()) {
                    for (size_t i = 0; i < fLineClusters.size(); ++i) {
                        if (fLineClusters[i].empty()) continue;
                        if (current.text_index >= fLineClusters[i].front().text_range.start &&
                            current.text_index <= fLineClusters[i].back().text_range.end) {
                            currentLineIdx = i;
                            break;
                        }
                    }
                }
                if (currentLineIdx > 0 && currentLineIdx < lines.size()) {
                    size_t targetLineIdx = currentLineIdx - 1;
                    SkScalar targetY = lines[targetLineIdx].bounds.centerY();
                    SkScalar targetX = current.caret_rect.fLeft;
                    next = hitTest(targetX, targetY);
                }
                break;
            }
            default:
                break;
        }

        return next;
    }

    TextRange getWordBoundary(TextIndex codepoint_index) const override {
        if (!fFormatted) {
            return TextRange(codepoint_index, codepoint_index);
        }
        auto wordBreaks = fFormatted->shaped().unicode().word_breaks();
        if (wordBreaks.empty()) {
            return TextRange(codepoint_index, codepoint_index);
        }

        TextIndex start(0);
        TextIndex end = wordBreaks.back();

        for (size_t i = 0; i < wordBreaks.size(); ++i) {
            if (wordBreaks[i] <= codepoint_index) {
                start = wordBreaks[i];
            }
            if (wordBreaks[i] > codepoint_index) {
                end = wordBreaks[i];
                break;
            }
        }
        return TextRange(start, end);
    }

    void getSelectionRects(TextRange range, std::vector<SkRect>& out_rects) const override {
        out_rects.clear();
        for (const auto& cb : fFlatClusters) {
            if (cb.text_range.start >= range.start && cb.text_range.end <= range.end) {
                out_rects.push_back(cb.bounds);
            }
        }
    }

    void getSelectionForVisualDrag(
        SkScalar x1, SkScalar y1, SkScalar x2, SkScalar y2,
        std::vector<SkRect>& out_rects,
        std::vector<TextRange>& out_ranges) const override {
        out_rects.clear();
        out_ranges.clear();

        if (!fFormatted || fFlatClusters.empty()) {
            return;
        }

        const auto& lines = fFormatted->lines();
        if (lines.empty()) {
            return;
        }

        auto getLineIdx = [&](SkScalar y) -> size_t {
            if (y < lines.front().bounds.fTop) {
                return 0;
            }
            for (size_t i = 0; i < lines.size(); ++i) {
                if (y <= lines[i].bounds.fBottom || i == lines.size() - 1) {
                    return i;
                }
            }
            return lines.size() - 1;
        };

        size_t line1 = getLineIdx(y1);
        size_t line2 = getLineIdx(y2);

        std::vector<TextRange> rawRanges;

        if (line1 == line2) {
            // 1D visual drag within a single line
            SkScalar minX = std::min(x1, x2);
            SkScalar maxX = std::max(x1, x2);

            if (minX < maxX && line1 < fLineClusters.size()) {
                const auto& clustersOnLine = fLineClusters[line1];
                for (const auto& cb : clustersOnLine) {
                    SkScalar cbLeft = std::min(cb.bounds.fLeft, cb.bounds.fRight);
                    SkScalar cbRight = std::max(cb.bounds.fLeft, cb.bounds.fRight);
                    bool overlaps = !(cbRight <= minX || cbLeft >= maxX);
                    if (overlaps) {
                        out_rects.push_back(cb.bounds);
                        rawRanges.push_back(cb.text_range);
                    }
                }
            }
        } else {
            // 2D multi-line visual drag crossing line boundaries
            size_t topLine = std::min(line1, line2);
            size_t bottomLine = std::max(line1, line2);
            SkScalar xTop = (line1 < line2) ? x1 : x2;
            SkScalar xBottom = (line1 < line2) ? x2 : x1;

            // 1. Top line: from xTop to line trailing edge [xTop, +infinity)
            if (topLine < fLineClusters.size()) {
                for (const auto& cb : fLineClusters[topLine]) {
                    SkScalar cbLeft = std::min(cb.bounds.fLeft, cb.bounds.fRight);
                    SkScalar cbRight = std::max(cb.bounds.fLeft, cb.bounds.fRight);
                    if (cbRight > xTop || (cbRight == cbLeft && cbLeft >= xTop)) {
                        out_rects.push_back(cb.bounds);
                        rawRanges.push_back(cb.text_range);
                    }
                }
            }

            // 2. Intermediate lines: 100% full line cluster saturation
            for (size_t k = topLine + 1; k < bottomLine; ++k) {
                if (k < fLineClusters.size()) {
                    for (const auto& cb : fLineClusters[k]) {
                        out_rects.push_back(cb.bounds);
                        rawRanges.push_back(cb.text_range);
                    }
                }
            }

            // 3. Bottom line: from line leading edge to xBottom (-infinity, xBottom]
            if (bottomLine < fLineClusters.size()) {
                for (const auto& cb : fLineClusters[bottomLine]) {
                    SkScalar cbLeft = std::min(cb.bounds.fLeft, cb.bounds.fRight);
                    SkScalar cbRight = std::max(cb.bounds.fLeft, cb.bounds.fRight);
                    if (cbLeft < xBottom || (cbRight == cbLeft && cbLeft <= xBottom)) {
                        out_rects.push_back(cb.bounds);
                        rawRanges.push_back(cb.text_range);
                    }
                }
            }
        }

        if (rawRanges.empty()) {
            return;
        }

        // Sort ranges logically and merge adjacent/overlapping spans
        std::sort(rawRanges.begin(), rawRanges.end(), [](const TextRange& a, const TextRange& b) {
            return a.start < b.start;
        });

        out_ranges.push_back(rawRanges[0]);
        for (size_t i = 1; i < rawRanges.size(); ++i) {
            if (rawRanges[i].start <= out_ranges.back().end) {
                out_ranges.back().end = std::max(out_ranges.back().end, rawRanges[i].end);
            } else {
                out_ranges.push_back(rawRanges[i]);
            }
        }

#if defined(SK_DEBUG)
        // Postcondition Integrity Assertion (Axiom 11 / Rule 8: Dimensional Honesty)
        if (line1 != line2) {
            SkASSERT(!out_ranges.empty() && "Multi-line visual drag must produce non-empty selection");
            size_t topLine = std::min(line1, line2);
            size_t bottomLine = std::max(line1, line2);
            for (size_t k = topLine + 1; k < bottomLine; ++k) {
                if (k < fLineClusters.size() && !fLineClusters[k].empty()) {
                    SkASSERT(!out_rects.empty() && "Intermediate lines must be saturated");
                }
            }
        }
#endif
    }

private:
    void buildIndex() {
        if (!fFormatted) {
            return;
        }

        const auto& lines = fFormatted->lines();
        fLineClusters.resize(lines.size());

        ClusterIndex clusterCounter(0);
        GlyphIndex glyphCounter(0);

        auto graphemeBreaks = fFormatted->shaped().unicode().grapheme_breaks();
        std::string_view fullText = fFormatted->shaped().unicode().text();

        for (size_t lIdx = 0; lIdx < lines.size(); ++lIdx) {
            const auto& line = lines[lIdx];
            for (const auto& vr : line.visual_runs) {
                SkScalar curX = vr.x_offset;
                for (const auto& g : vr.glyphs) {
                    TextIndex endIdx = g.cluster_text_index + 1;
                    if (g.cluster_text_index.value < fullText.size()) {
                        const char* ptr = fullText.data() + g.cluster_text_index.value;
                        const char* end = fullText.data() + fullText.size();
                        SkUTF::NextUTF8(&ptr, end);
                        endIdx = TextIndex(ptr - fullText.data());
                    }

                    if (!graphemeBreaks.empty()) {
                        auto it = std::upper_bound(graphemeBreaks.begin(), graphemeBreaks.end(), g.cluster_text_index);
                        if (it != graphemeBreaks.end() && *it > endIdx) {
                            endIdx = *it;
                        }
                    }

                    SkScalar cbWidth = g.advance.fX;
                    SkScalar cbHeight = std::abs(vr.ascent) + std::abs(vr.descent);
                    if (cbHeight <= 0) {
                        cbHeight = 16.0f;
                    }

                    // Check if this glyph should be merged into the previous cluster on this line
                    if (!fLineClusters[lIdx].empty()) {
                        ClusterBox& lastCb = fLineClusters[lIdx].back();
                        if (g.is_mark || lastCb.text_range.contains(g.cluster_text_index) || g.cluster_text_index == lastCb.text_range.start) {
                            GlyphIndex curGlyph = glyphCounter;
                            glyphCounter = glyphCounter + 1;

                            lastCb.text_range.end = std::max(lastCb.text_range.end, endIdx);
                            fFlatClusters.back().text_range.end = lastCb.text_range.end;

                            lastCb.glyph_range.end = GlyphIndex(curGlyph.value + 1);
                            fFlatClusters.back().glyph_range.end = lastCb.glyph_range.end;

                            // Keep cluster vertical bounds strictly typographic (line.typographic_ascent / descent)
                            // Diacritic ink bounds are tracked by LineBox::bounds for invalidation.
                            lastCb.bounds.fRight += g.advance.fX;
                            fFlatClusters.back().bounds = lastCb.bounds;

                            curX += g.advance.fX;
                            continue;
                        }
                    }

                    ClusterIndex curCluster = clusterCounter;
                    clusterCounter = clusterCounter + 1;
                    GlyphIndex curGlyph = glyphCounter;
                    glyphCounter = glyphCounter + 1;

                    ClusterBox cb;
                    cb.cluster_index = curCluster;
                    cb.text_range = TextRange(g.cluster_text_index, endIdx);
                    cb.glyph_range = GlyphRange(curGlyph, curGlyph + 1);
                    cb.is_rtl = vr.isRTL();

                    cb.bounds = SkRect::MakeXYWH(curX + g.offset.fX,
                                                 line.baseline + vr.ascent,
                                                 cbWidth,
                                                 cbHeight);
                    curX += g.advance.fX;

                    fLineClusters[lIdx].push_back(cb);
                    fFlatClusters.push_back(cb);
                }
            }
        }

        fLogicalClusters = fFlatClusters;
        std::sort(fLogicalClusters.begin(), fLogicalClusters.end(),
                  [](const ClusterBox& a, const ClusterBox& b) {
                      return a.text_range.start < b.text_range.start;
                  });
    }

    std::shared_ptr<const FormattedParagraph> fFormatted;
    std::vector<std::vector<ClusterBox>> fLineClusters;
    std::vector<ClusterBox> fFlatClusters;
    std::vector<ClusterBox> fLogicalClusters;
};

} // namespace

std::unique_ptr<const ParagraphSpatialIndex> ParagraphSpatialIndex::Make(
    std::shared_ptr<const FormattedParagraph> formatted_para)
{
    return std::make_unique<ParagraphSpatialIndexImpl>(std::move(formatted_para));
}

} // namespace skia::text_editor
