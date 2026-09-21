/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/FormattedParagraph.h"
#include "include/core/SkFontTypes.h"
#include "src/base/SkUTF.h"
#include <algorithm>
#include <cmath>

namespace skia::text_editor {

namespace {

class FormattedParagraphImpl : public FormattedParagraph {
public:
    FormattedParagraphImpl(
        std::shared_ptr<const ShapedParagraph> shaped_para,
        const LayoutConstraints& constraints)
        : fShaped(std::move(shaped_para))
        , fWidth(0)
        , fHeight(0)
    {
        layout(constraints);
    }

    const ShapedParagraph& shaped() const override { return *fShaped; }
    SkSpan<const LineBox> lines() const override { return SkSpan<const LineBox>(fLines); }
    SkScalar width() const override { return fWidth; }
    SkScalar height() const override { return fHeight; }

    void visitParagraphRuns(const SkRect& localClip, RenderRunVisitor visitor) const override {
        if (!visitor) {
            return;
        }
        for (const auto& line : fLines) {
            if (!line.bounds.intersects(localClip)) {
                continue;
            }
            for (const auto& vr : line.visual_runs) {
                if (vr.glyph_ids.empty()) {
                    continue;
                }
                RenderRun run{
                    vr.font,
                    vr.color,
                    SkSpan<const SkGlyphID>(vr.glyph_ids),
                    SkSpan<const SkPoint>(vr.glyph_positions),
                };
                visitor(run);
            }
        }
    }

private:
    void layout(const LayoutConstraints& constraints) {
        if (!fShaped) {
            return;
        }

        const auto& shapedRuns = fShaped->shaped_runs();
        if (shapedRuns.empty()) {
            return;
        }

        SkScalar defaultAscent = shapedRuns[0].ascent;
        SkScalar defaultDescent = shapedRuns[0].descent;
        if (defaultAscent == 0 && defaultDescent == 0) {
            defaultAscent = -14.0f * 0.8f;
            defaultDescent = 14.0f * 0.2f;
        }

        SkScalar yCursor = 0;

        std::string_view fullText = fShaped->unicode().text();
        LineBox currentLine;
        currentLine.line_index = fLines.size();
        currentLine.baseline = 0;
        SkScalar lineWidth = 0;
        SkScalar lineAscent = 0;
        SkScalar lineDescent = 0;

        auto flushLine = [&](bool isLastLine) {
            if (currentLine.visual_runs.empty()) {
                if (fLines.empty() && isLastLine) {
                    return;
                }
                // Handle empty lines gracefully
                size_t emptyStart = fLines.empty() ? 0 : fLines.back().text_range.end.value;
                currentLine.text_range = TextRange(TextIndex(emptyStart), TextIndex(emptyStart));
                currentLine.baseline = yCursor + std::abs(defaultAscent);
                SkScalar lineHeight = std::abs(defaultAscent) + std::abs(defaultDescent);
                currentLine.bounds = SkRect::MakeXYWH(0, yCursor, 0, lineHeight);
                currentLine.ascent = defaultAscent;
                currentLine.descent = defaultDescent;
                currentLine.typographic_ascent = defaultAscent;
                currentLine.typographic_descent = defaultDescent;
                currentLine.total_width = 0;
                yCursor += lineHeight;
                fLines.push_back(std::move(currentLine));

                currentLine = LineBox();
                currentLine.line_index = fLines.size();
                lineWidth = 0;
                lineAscent = 0;
                lineDescent = 0;
                return;
            }

            // Compute text_range for non-empty line
            size_t minStart = SIZE_MAX;
            size_t maxEnd = 0;
            for (const auto& vr : currentLine.visual_runs) {
                for (const auto& g : vr.glyphs) {
                    minStart = std::min(minStart, g.cluster_text_index.value);
                    size_t endIdx = g.cluster_text_index.value + 1;
                    if (g.cluster_text_index.value < fullText.size()) {
                        const char* ptr = fullText.data() + g.cluster_text_index.value;
                        const char* end = fullText.data() + fullText.size();
                        SkUTF::NextUTF8(&ptr, end);
                        endIdx = ptr - fullText.data();
                    }
                    maxEnd = std::max(maxEnd, endIdx);
                }
            }
            if (minStart != SIZE_MAX) {
                currentLine.text_range = TextRange(TextIndex(minStart), TextIndex(maxEnd));
            }

            // Apply horizontal text alignment (bake offset into visual runs)
            SkScalar xShift = 0;
            SkScalar availableWidth = constraints.max_width;
            if (std::isfinite(availableWidth) && availableWidth > currentLine.content_width) {
                switch (constraints.align) {
                    case TextAlign::kCenter:
                        xShift = (availableWidth - currentLine.content_width) / 2.0f;
                        break;
                    case TextAlign::kRight:
                    case TextAlign::kEnd:
                        xShift = availableWidth - currentLine.content_width;
                        break;
                    default:
                        xShift = 0;
                        break;
                }
            }

            SkScalar currentX = xShift;
            SkScalar maxZalgoTop = 0;
            SkScalar maxZalgoBottom = 0;

            for (auto& vr : currentLine.visual_runs) {
                vr.x_offset = currentX;
                for (const auto& g : vr.glyphs) {
                    // Check diacritic vertical offsets for Zalgo height expansion
                    if (g.is_mark) {
                        if (-g.offset.fY > maxZalgoTop) {
                            maxZalgoTop = -g.offset.fY;
                        }
                        if (g.offset.fY > maxZalgoBottom) {
                            maxZalgoBottom = g.offset.fY;
                        }
                    }
                }
                currentX += vr.width;
            }

            if (lineAscent == 0 && lineDescent == 0) {
                lineAscent = defaultAscent;
                lineDescent = defaultDescent;
            }

            currentLine.baseline = yCursor + std::abs(lineAscent) + maxZalgoTop;
            SkScalar lineHeight = (std::abs(lineAscent) + maxZalgoTop) + (std::abs(lineDescent) + maxZalgoBottom);
            currentLine.bounds = SkRect::MakeXYWH(xShift, yCursor, currentLine.content_width, lineHeight);
            currentLine.ascent = lineAscent - maxZalgoTop;
            currentLine.descent = lineDescent + maxZalgoBottom;
            currentLine.typographic_ascent = lineAscent;
            currentLine.typographic_descent = lineDescent;
            currentLine.total_width = lineWidth;

            // Precompute flat glyph arrays for zero-allocation rendering
            for (auto& vr : currentLine.visual_runs) {
                vr.glyph_ids.reserve(vr.glyphs.size());
                vr.glyph_positions.reserve(vr.glyphs.size());
                SkScalar curGlyphX = vr.x_offset;
                for (const auto& g : vr.glyphs) {
                    if (!g.is_zero_width_control) {
                        vr.glyph_ids.push_back(static_cast<SkGlyphID>(g.glyph_id));
                        vr.glyph_positions.push_back(SkPoint::Make(curGlyphX + g.offset.fX, currentLine.baseline + g.offset.fY));
                    }
                    curGlyphX += g.advance.fX;
                }
            }

            yCursor += lineHeight;
            fLines.push_back(std::move(currentLine));

            currentLine = LineBox();
            currentLine.line_index = fLines.size();
            lineWidth = 0;
            lineAscent = 0;
            lineDescent = 0;
        };

        // Precompute line break offsets from UnicodeParagraph and whitespace
        std::vector<size_t> breakOffsets;
        for (const auto& lb : fShaped->unicode().line_breaks()) {
            if (lb.offset.value > 0) {
                breakOffsets.push_back(lb.offset.value);
            }
        }
        for (size_t i = 0; i < fullText.size(); ++i) {
            if (fShaped->unicode().isSpace(TextIndex(i)) || fullText[i] == ' ') {
                breakOffsets.push_back(i + 1);
            }
        }
        std::sort(breakOffsets.begin(), breakOffsets.end());
        breakOffsets.erase(std::unique(breakOffsets.begin(), breakOffsets.end()), breakOffsets.end());

        VisualRun vr;
        bool vrInitialized = false;

        auto startNewVR = [&](const ShapedRun& sr) {
            if (vrInitialized && !vr.glyphs.empty()) {
                currentLine.visual_runs.push_back(std::move(vr));
            }
            vr = VisualRun();
            vr.font = sr.item->font;
            vr.bidi_level = sr.item->bidi_level;
            vr.ascent = sr.ascent;
            vr.descent = sr.descent;
            vr.text_range = sr.item->text_range;
            vrInitialized = true;
        };

        auto appendEllipsis = [&](const ShapedRun& sr) {
            if (!constraints.ellipsis.empty()) {
                ShapedGlyph eg;
                SkUnichar uEllipsis = 0x2026;
                sr.item->font.textToGlyphs(&uEllipsis, sizeof(SkUnichar), SkTextEncoding::kUTF32,
                                           reinterpret_cast<SkGlyphID*>(&eg.glyph_id), 1);
                SkScalar eWidth = 0;
                sr.item->font.getWidths(reinterpret_cast<SkGlyphID*>(&eg.glyph_id), 1, &eWidth);
                if (eWidth <= 0) {
                    eWidth = sr.item->font.getSize() > 0 ? sr.item->font.getSize() * 0.6f : 8.0f;
                }
                eg.advance = SkPoint::Make(eWidth, 0);
                eg.offset = SkPoint::Make(0, 0);
                eg.cluster_text_index = vr.glyphs.empty() ? TextIndex(0) : vr.glyphs.back().cluster_text_index;
                eg.is_mark = false;
                eg.is_zero_width_control = false;
                vr.glyphs.push_back(eg);
                vr.width += eWidth;
                lineWidth += eWidth;
                currentLine.content_width += eWidth;
                vr.is_ellipsis_terminated = true;
                currentLine.has_ellipsis = true;
            }
        };

        struct Chunk {
            size_t start_gidx;
            size_t end_gidx;
            SkScalar width;
            bool ends_with_hard_break;
        };

        for (size_t runIdx = 0; runIdx < shapedRuns.size(); ++runIdx) {
            const auto& sr = shapedRuns[runIdx];
            if (sr.glyphs.empty()) {
                continue;
            }

            if (!vrInitialized || vr.font.getTypeface() != sr.item->font.getTypeface() || vr.bidi_level != sr.item->bidi_level) {
                startNewVR(sr);
            }

            lineAscent = std::min(lineAscent, sr.ascent);
            lineDescent = std::max(lineDescent, sr.descent);

            // Break shaped run into chunks (words/tokens) based on break opportunities
            std::vector<Chunk> chunks;
            size_t curStart = 0;
            SkScalar curW = 0;

            for (size_t gIdx = 0; gIdx < sr.glyphs.size(); ++gIdx) {
                const auto& g = sr.glyphs[gIdx];
                bool isHard = fShaped->unicode().isHardLineBreak(g.cluster_text_index) ||
                              (g.cluster_text_index.value < fullText.size() && fullText[g.cluster_text_index.value] == '\n');
                bool isSoftBreak = (gIdx > curStart) &&
                                   std::binary_search(breakOffsets.begin(), breakOffsets.end(), g.cluster_text_index.value);

                if (isSoftBreak) {
                    chunks.push_back({curStart, gIdx, curW, false});
                    curStart = gIdx;
                    curW = 0;
                }

                curW += g.advance.fX;

                if (isHard) {
                    chunks.push_back({curStart, gIdx + 1, curW, true});
                    curStart = gIdx + 1;
                    curW = 0;
                }
            }

            if (curStart < sr.glyphs.size()) {
                chunks.push_back({curStart, sr.glyphs.size(), curW, false});
            }

            // Place chunks onto lines
            for (const auto& chunk : chunks) {
                bool chunkFits = !std::isfinite(constraints.max_width) || (lineWidth + chunk.width <= constraints.max_width);

                if (!chunkFits && lineWidth > 0) {
                    // Current line cannot fit this chunk; try wrapping to next line
                    if (constraints.max_lines > 0 && fLines.size() + 1 >= constraints.max_lines) {
                        appendEllipsis(sr);
                        if (!vr.glyphs.empty()) {
                            currentLine.visual_runs.push_back(std::move(vr));
                            vrInitialized = false;
                        }
                        flushLine(true);
                        goto layout_finished;
                    }

                    // Soft wrap to next line
                    if (!vr.glyphs.empty()) {
                        currentLine.visual_runs.push_back(std::move(vr));
                        vrInitialized = false;
                    }
                    flushLine(false);
                    startNewVR(sr);
                    lineAscent = sr.ascent;
                    lineDescent = sr.descent;
                }

                // Check if chunk fits on the current line (which could be freshly started)
                if (!std::isfinite(constraints.max_width) || (lineWidth + chunk.width <= constraints.max_width)) {
                    for (size_t gi = chunk.start_gidx; gi < chunk.end_gidx; ++gi) {
                        vr.glyphs.push_back(sr.glyphs[gi]);
                    }
                    vr.width += chunk.width;
                    lineWidth += chunk.width;
                    currentLine.content_width += chunk.width;

                    if (chunk.ends_with_hard_break) {
                        currentLine.has_hard_break = true;
                        if (!vr.glyphs.empty()) {
                            currentLine.visual_runs.push_back(std::move(vr));
                            vrInitialized = false;
                        }
                        flushLine(false);
                        startNewVR(sr);
                        lineAscent = sr.ascent;
                        lineDescent = sr.descent;
                    }
                } else {
                    // Oversized chunk on empty/near-empty line: break inside chunk glyph-by-glyph
                    for (size_t gi = chunk.start_gidx; gi < chunk.end_gidx; ++gi) {
                        const auto& g = sr.glyphs[gi];
                        SkScalar gWidth = g.advance.fX;

                        if (std::isfinite(constraints.max_width) && (lineWidth + gWidth > constraints.max_width) && lineWidth > 0) {
                            if (constraints.max_lines > 0 && fLines.size() + 1 >= constraints.max_lines) {
                                appendEllipsis(sr);
                                if (!vr.glyphs.empty()) {
                                    currentLine.visual_runs.push_back(std::move(vr));
                                    vrInitialized = false;
                                }
                                flushLine(true);
                                goto layout_finished;
                            }
                            if (!vr.glyphs.empty()) {
                                currentLine.visual_runs.push_back(std::move(vr));
                                vrInitialized = false;
                            }
                            flushLine(false);
                            startNewVR(sr);
                            lineAscent = sr.ascent;
                            lineDescent = sr.descent;
                        }

                        vr.glyphs.push_back(g);
                        vr.width += gWidth;
                        lineWidth += gWidth;
                        currentLine.content_width += gWidth;

                        bool isHard = fShaped->unicode().isHardLineBreak(g.cluster_text_index) ||
                                      (g.cluster_text_index.value < fullText.size() && fullText[g.cluster_text_index.value] == '\n');
                        if (isHard) {
                            currentLine.has_hard_break = true;
                            if (!vr.glyphs.empty()) {
                                currentLine.visual_runs.push_back(std::move(vr));
                                vrInitialized = false;
                            }
                            flushLine(false);
                            startNewVR(sr);
                            lineAscent = sr.ascent;
                            lineDescent = sr.descent;
                        }
                    }
                }
            }
        }

        if (vrInitialized && !vr.glyphs.empty()) {
            currentLine.visual_runs.push_back(std::move(vr));
        }
        flushLine(true);

    layout_finished:
        for (const auto& line : fLines) {
            fWidth = std::max(fWidth, line.total_width);
        }
        fHeight = yCursor;
    }

    std::shared_ptr<const ShapedParagraph> fShaped;
    std::vector<LineBox> fLines;
    SkScalar fWidth;
    SkScalar fHeight;
};

} // namespace

std::unique_ptr<const FormattedParagraph> FormattedParagraph::Make(
    std::shared_ptr<const ShapedParagraph> shaped_para,
    const LayoutConstraints& constraints)
{
    return std::make_unique<FormattedParagraphImpl>(std::move(shaped_para), constraints);
}

} // namespace skia::text_editor
