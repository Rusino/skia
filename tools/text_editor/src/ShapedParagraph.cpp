/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/ShapedParagraph.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkStream.h"
#include "include/core/SkTypeface.h"
#include "src/base/SkUTF.h"
#include <hb.h>
#include <hb-ot.h>

namespace skia::text_editor {

namespace {

// Helper for HarfBuzz table loading from SkTypeface
static hb_blob_t* skhb_get_table(hb_face_t* face, hb_tag_t tag, void* user_data) {
    SkTypeface* typeface = reinterpret_cast<SkTypeface*>(user_data);
    auto data = typeface->copyTableData(tag);
    if (!data) {
        return nullptr;
    }
    SkData* rawData = data.release();
    return hb_blob_create(reinterpret_cast<char*>(rawData->writable_data()), rawData->size(),
                          HB_MEMORY_MODE_READONLY, rawData, [](void* ctx) {
                              SkSafeUnref(reinterpret_cast<SkData*>(ctx));
                          });
}

class ShapedParagraphImpl : public ShapedParagraph {
public:
    ShapedParagraphImpl(std::shared_ptr<const UnicodeParagraph> unicode_para)
        : fUnicode(std::move(unicode_para))
        , fAdvanceWidth(0)
    {
        shapeRuns();
    }

    const UnicodeParagraph& unicode() const override { return *fUnicode; }
    SkSpan<const ShapedRun> shaped_runs() const override { return SkSpan<const ShapedRun>(fRuns); }
    SkScalar advance_width() const override { return fAdvanceWidth; }

private:
    void shapeRuns() {
        if (!fUnicode) {
            return;
        }

        std::string_view fullText = fUnicode->text();

        for (const auto& item : fUnicode->runs()) {
            ShapedRun shaped;
            shaped.item = &item;

            SkFontMetrics metrics;
            item.font.getMetrics(&metrics);
            if (metrics.fAscent == 0 && metrics.fDescent == 0) {
                float sz = item.font.getSize() > 0 ? item.font.getSize() : 14.0f;
                shaped.ascent = -sz * 0.8f;
                shaped.descent = sz * 0.2f;
            } else {
                shaped.ascent = metrics.fAscent;
                shaped.descent = metrics.fDescent;
            }
            shaped.leading = metrics.fLeading;

            size_t runStart = item.text_range.start.value;
            size_t runLen = item.text_range.length();
            if (runLen == 0) {
                fRuns.push_back(std::move(shaped));
                continue;
            }

            const char* runText = fullText.data() + runStart;

            // 1. Initialize HarfBuzz buffer with strictly preserved 1-to-1 character cluster mapping
            hb_buffer_t* buf = hb_buffer_create();
            hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_CHARACTERS);
            hb_buffer_add_utf8(buf, fullText.data(), fullText.size(), runStart, runLen);
            hb_buffer_set_direction(buf, item.direction == Direction::kRTL ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            hb_buffer_guess_segment_properties(buf);

            // 2. Load HarfBuzz font face from SkTypeface
            sk_sp<SkTypeface> typeface = item.font.refTypeface();
            hb_face_t* hbFace = nullptr;
            if (typeface) {
                int ttcIndex = 0;
                std::unique_ptr<SkStreamAsset> stream = typeface->openStream(&ttcIndex);
                if (stream) {
                    size_t streamLen = stream->getLength();
                    sk_sp<SkData> data = SkData::MakeUninitialized(streamLen);
                    if (stream->read(data->writable_data(), streamLen) == streamLen) {
                        hb_blob_t* blob = hb_blob_create((const char*)data->data(), data->size(),
                                                         HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr);
                        hbFace = hb_face_create(blob, ttcIndex);
                        hb_blob_destroy(blob);
                    }
                }
                if (!hbFace) {
                    hbFace = hb_face_create_for_tables(skhb_get_table, typeface.get(), nullptr);
                    hb_face_set_index(hbFace, (unsigned)ttcIndex);
                }
            }

            hb_font_t* hbFont = hbFace ? hb_font_create(hbFace) : nullptr;
            if (hbFont) {
                hb_ot_font_set_funcs(hbFont);
                float fontSize = item.font.getSize();
                if (fontSize <= 0) {
                    fontSize = 14.0f;
                }
                // HarfBuzz 16.16 fixed-point scale
                int scale = (int)(fontSize * 64.0f);
                hb_font_set_scale(hbFont, scale, scale);
            }

            // 3. Negative Constraints: Disable liga and ccmp to guarantee 1-to-1 mapping
            hb_feature_t features[] = {
                { HB_TAG('l', 'i', 'g', 'a'), 0, 0, (unsigned int)-1 },
                { HB_TAG('c', 'c', 'm', 'p'), 0, 0, (unsigned int)-1 },
                { HB_TAG('d', 'l', 'i', 'g'), 0, 0, (unsigned int)-1 },
                { HB_TAG('c', 'a', 'l', 't'), 0, 0, (unsigned int)-1 },
            };

            if (hbFont) {
                hb_shape(hbFont, buf, features, sizeof(features) / sizeof(features[0]));
            }

            // 4. Extract shaped glyphs
            unsigned int glyphCount = 0;
            hb_glyph_info_t* glyphInfos = hb_buffer_get_glyph_infos(buf, &glyphCount);
            hb_glyph_position_t* glyphPositions = hb_buffer_get_glyph_positions(buf, &glyphCount);

            if (glyphCount == 0 || !hbFont) {
                // Fallback glyph generation from SkFont if HarfBuzz font face was unavailable
                const char* ptr = runText;
                const char* end = runText + runLen;
                while (ptr < end) {
                    TextIndex codepointOffset(ptr - fullText.data());
                    SkUnichar u = SkUTF::NextUTF8(&ptr, end);
                    SkGlyphID gid = 0;
                    item.font.textToGlyphs(&u, sizeof(SkUnichar), SkTextEncoding::kUTF32, &gid, 1);
                    SkScalar width = 0;
                    item.font.getWidths(&gid, 1, &width);

                    ShapedGlyph sg;
                    sg.glyph_id = gid;
                    sg.cluster_text_index = codepointOffset;
                    sg.offset = SkPoint::Make(0, 0);
                    sg.is_mark = (u >= 0x0300 && u <= 0x036F) || (u >= 0x1AB0 && u <= 0x1AFF) ||
                                 (u >= 0x1DC0 && u <= 0x1DFF) || (u >= 0xFE20 && u <= 0xFE2F);
                    sg.is_zero_width_control = fUnicode->isControl(codepointOffset);

                    if (width <= 0 && !sg.is_mark && !sg.is_zero_width_control) {
                        width = item.font.getSize() > 0 ? item.font.getSize() * 0.6f : 8.0f;
                    }
                    sg.advance = SkPoint::Make(width, 0);

                    shaped.width += width;
                    shaped.glyphs.push_back(sg);
                }
            } else {
                for (unsigned int i = 0; i < glyphCount; ++i) {
                    ShapedGlyph sg;
                    sg.glyph_id = glyphInfos[i].codepoint;
                    sg.cluster_text_index = TextIndex(glyphInfos[i].cluster);

                    SkUnichar u = 0;
                    if (sg.cluster_text_index.value < fullText.size()) {
                        const char* p = fullText.data() + sg.cluster_text_index.value;
                        u = SkUTF::NextUTF8(&p, fullText.data() + fullText.size());
                    }
                    sg.is_mark = (u >= 0x0300 && u <= 0x036F) || (u >= 0x1AB0 && u <= 0x1AFF) ||
                                 (u >= 0x1DC0 && u <= 0x1DFF) || (u >= 0xFE20 && u <= 0xFE2F);
                    sg.is_zero_width_control = fUnicode->isControl(sg.cluster_text_index);

                    SkScalar w = glyphPositions[i].x_advance / 64.0f;
                    if (w <= 0 && !sg.is_mark && !sg.is_zero_width_control) {
                        w = item.font.getSize() > 0 ? item.font.getSize() * 0.6f : 8.0f;
                    }
                    sg.advance = SkPoint::Make(w, glyphPositions[i].y_advance / 64.0f);
                    sg.offset = SkPoint::Make(glyphPositions[i].x_offset / 64.0f,
                                             glyphPositions[i].y_offset / 64.0f);

                    shaped.width += sg.advance.fX;
                    shaped.glyphs.push_back(sg);
                }
            }

            if (hbFont) {
                hb_font_destroy(hbFont);
            }
            if (hbFace) {
                hb_face_destroy(hbFace);
            }
            hb_buffer_destroy(buf);

            fAdvanceWidth += shaped.width;
            fRuns.push_back(std::move(shaped));
        }
    }

    std::shared_ptr<const UnicodeParagraph> fUnicode;
    std::vector<ShapedRun> fRuns;
    SkScalar fAdvanceWidth;
};

} // namespace

std::unique_ptr<const ShapedParagraph> ShapedParagraph::Make(
    std::shared_ptr<const UnicodeParagraph> unicode_para)
{
    return std::make_unique<ShapedParagraphImpl>(std::move(unicode_para));
}

} // namespace skia::text_editor
