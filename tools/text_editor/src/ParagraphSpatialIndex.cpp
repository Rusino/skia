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
        pos.caret_rect = SkRect::MakeXYWH(line.bounds.fLeft, line.baseline + line.ascent, 1.0f,
                                          std::abs(line.ascent) + std::abs(line.descent));

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
                    pos.text_index = cb.text_range.start;
                    pos.affinity = Affinity::kDownstream;
                    pos.caret_rect.fLeft = cb.bounds.fLeft;
                    pos.caret_rect.fRight = cb.bounds.fLeft + 1.0f;
                    return pos;
                } else if (x <= cb.bounds.fRight) {
                    pos.text_index = cb.text_range.end;
                    pos.affinity = Affinity::kUpstream;
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
                pos.text_index = lastCb.text_range.end;
                pos.affinity = Affinity::kUpstream;
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
            case CursorDirection::kRight:
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
            case CursorDirection::kLeft:
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

                            SkScalar markTop = line.baseline + vr.ascent + g.offset.fY;
                            SkScalar markBottom = markTop + cbHeight;
                            if (markTop < lastCb.bounds.fTop) {
                                lastCb.bounds.fTop = markTop;
                            }
                            if (markBottom > lastCb.bounds.fBottom) {
                                lastCb.bounds.fBottom = markBottom;
                            }
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
