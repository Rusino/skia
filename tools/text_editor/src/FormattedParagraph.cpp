/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/FormattedParagraph.h"
#include "include/core/SkFontTypes.h"
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

private:
    void layout(const LayoutConstraints& constraints) {
        if (!fShaped) {
            return;
        }

        const auto& shapedRuns = fShaped->shaped_runs();
        if (shapedRuns.empty()) {
            return;
        }

        SkScalar yCursor = 0;

        // Simple line breaking greedy accumulator across shaped runs
        LineBox currentLine;
        currentLine.line_index = fLines.size();
        currentLine.baseline = 0;
        SkScalar lineWidth = 0;
        SkScalar lineAscent = 0;
        SkScalar lineDescent = 0;

        auto flushLine = [&](bool isLastLine) {
            if (currentLine.visual_runs.empty()) {
                return;
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

            currentLine.baseline = yCursor + std::abs(lineAscent) + maxZalgoTop;
            SkScalar lineHeight = (std::abs(lineAscent) + maxZalgoTop) + (std::abs(lineDescent) + maxZalgoBottom);
            currentLine.bounds = SkRect::MakeXYWH(xShift, yCursor, currentLine.content_width, lineHeight);
            currentLine.ascent = lineAscent - maxZalgoTop;
            currentLine.descent = lineDescent + maxZalgoBottom;
            currentLine.total_width = lineWidth;

            yCursor += lineHeight;
            fLines.push_back(std::move(currentLine));

            currentLine = LineBox();
            currentLine.line_index = fLines.size();
            lineWidth = 0;
            lineAscent = 0;
            lineDescent = 0;
        };

        for (size_t runIdx = 0; runIdx < shapedRuns.size(); ++runIdx) {
            const auto& sr = shapedRuns[runIdx];
            if (sr.glyphs.empty()) {
                continue;
            }

            lineAscent = std::min(lineAscent, sr.ascent);
            lineDescent = std::max(lineDescent, sr.descent);

            VisualRun vr;
            vr.font = sr.item->font;
            vr.bidi_level = sr.item->bidi_level;
            vr.ascent = sr.ascent;
            vr.descent = sr.descent;
            vr.text_range = sr.item->text_range;

            for (size_t gIdx = 0; gIdx < sr.glyphs.size(); ++gIdx) {
                const auto& g = sr.glyphs[gIdx];
                SkScalar gWidth = g.advance.fX;

                // Check line truncation constraints
                if (std::isfinite(constraints.max_width) && (lineWidth + gWidth > constraints.max_width) && lineWidth > 0) {
                    if (constraints.max_lines > 0 && fLines.size() + 1 >= constraints.max_lines) {
                        // Append terminal ellipsis
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
                            eg.cluster_text_index = g.cluster_text_index;
                            eg.is_mark = false;
                            eg.is_zero_width_control = false;
                            vr.glyphs.push_back(eg);
                            vr.width += eWidth;
                            lineWidth += eWidth;
                            currentLine.content_width += eWidth;
                            vr.is_ellipsis_terminated = true;
                            currentLine.has_ellipsis = true;
                        }
                        currentLine.visual_runs.push_back(std::move(vr));
                        flushLine(true);
                        goto layout_finished;
                    } else {
                        // Soft wrap to next line
                        if (!vr.glyphs.empty()) {
                            currentLine.visual_runs.push_back(std::move(vr));
                            vr = VisualRun();
                            vr.font = sr.item->font;
                            vr.bidi_level = sr.item->bidi_level;
                            vr.ascent = sr.ascent;
                            vr.descent = sr.descent;
                            vr.text_range = sr.item->text_range;
                        }
                        flushLine(false);
                        lineAscent = sr.ascent;
                        lineDescent = sr.descent;
                    }
                }

                vr.glyphs.push_back(g);
                vr.width += gWidth;
                lineWidth += gWidth;
                currentLine.content_width += gWidth;
            }

            if (!vr.glyphs.empty()) {
                currentLine.visual_runs.push_back(std::move(vr));
            }
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
