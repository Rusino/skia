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
            for (const auto& cb : clusters) {
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
            pos.text_index = lastCb.text_range.end;
            pos.affinity = Affinity::kUpstream;
            pos.caret_rect.fLeft = lastCb.bounds.fRight;
            pos.caret_rect.fRight = lastCb.bounds.fRight + 1.0f;
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
        if (fFlatClusters.empty()) {
            return next;
        }

        // 1. Locate cluster for current text index
        size_t currentIdx = fFlatClusters.size();
        for (size_t i = 0; i < fFlatClusters.size(); ++i) {
            if (fFlatClusters[i].text_range.contains(current.text_index)) {
                currentIdx = i;
                break;
            }
        }
        bool atEndOfText = (current.text_index >= fFlatClusters.back().text_range.end);

        switch (dir) {
            case CursorDirection::kRight:
                if (mode == NavigationMode::kScreenPhysical || mode == NavigationMode::kTextLogical) {
                    if (currentIdx < fFlatClusters.size()) {
                        if (currentIdx + 1 < fFlatClusters.size()) {
                            next.text_index = fFlatClusters[currentIdx + 1].text_range.start;
                            next.affinity = Affinity::kDownstream;
                            next.caret_rect = fFlatClusters[currentIdx + 1].bounds;
                            next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                        } else {
                            // Advance past the last cluster to end of text
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
                if (mode == NavigationMode::kScreenPhysical || mode == NavigationMode::kTextLogical) {
                    if (atEndOfText) {
                        // Move from end of text to start of last cluster
                        next.text_index = fFlatClusters.back().text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fFlatClusters.back().bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    } else if (currentIdx > 0 && currentIdx < fFlatClusters.size()) {
                        next.text_index = fFlatClusters[currentIdx - 1].text_range.start;
                        next.affinity = Affinity::kDownstream;
                        next.caret_rect = fFlatClusters[currentIdx - 1].bounds;
                        next.caret_rect.fRight = next.caret_rect.fLeft + 1.0f;
                    }
                }
                break;
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

        for (size_t lIdx = 0; lIdx < lines.size(); ++lIdx) {
            const auto& line = lines[lIdx];
            for (const auto& vr : line.visual_runs) {
                SkScalar curX = vr.x_offset;
                for (const auto& g : vr.glyphs) {
                    std::string_view fullText = fFormatted->shaped().unicode().text();
                    TextIndex endIdx = g.cluster_text_index + 1;
                    if (g.cluster_text_index.value < fullText.size()) {
                        const char* ptr = fullText.data() + g.cluster_text_index.value;
                        const char* end = fullText.data() + fullText.size();
                        SkUTF::NextUTF8(&ptr, end);
                        endIdx = TextIndex(ptr - fullText.data());
                    }

                    ClusterIndex curCluster = clusterCounter;
                    clusterCounter = clusterCounter + 1;
                    GlyphIndex curGlyph = glyphCounter;
                    glyphCounter = glyphCounter + 1;

                    ClusterBox cb;
                    cb.cluster_index = curCluster;
                    cb.text_range = TextRange(g.cluster_text_index, endIdx);
                    cb.glyph_range = GlyphRange(curGlyph, curGlyph + 1);

                    SkScalar cbWidth = g.advance.fX;
                    SkScalar cbHeight = std::abs(vr.ascent) + std::abs(vr.descent);
                    if (cbHeight <= 0) {
                        cbHeight = 16.0f;
                    }

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
    }

    std::shared_ptr<const FormattedParagraph> fFormatted;
    std::vector<std::vector<ClusterBox>> fLineClusters;
    std::vector<ClusterBox> fFlatClusters;
};

} // namespace

std::unique_ptr<const ParagraphSpatialIndex> ParagraphSpatialIndex::Make(
    std::shared_ptr<const FormattedParagraph> formatted_para)
{
    return std::make_unique<ParagraphSpatialIndexImpl>(std::move(formatted_para));
}

} // namespace skia::text_editor
