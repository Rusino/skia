/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/UnicodeParagraph.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "src/base/SkUTF.h"
#include <algorithm>

namespace skia::text_editor {

namespace {

class UnicodeParagraphImpl : public UnicodeParagraph {
public:
    UnicodeParagraphImpl(std::string_view utf8_text, SkSpan<const StyleSpan> styles)
        : fText(utf8_text)
        , fUnicode(SkUnicode::Make())
    {
        init(styles);
    }

    std::string_view text() const override { return fText; }
    SkSpan<const ItemizedRun> runs() const override { return SkSpan<const ItemizedRun>(fRuns); }
    SkSpan<const TextIndex> grapheme_breaks() const override { return SkSpan<const TextIndex>(fGraphemeBreaks); }
    SkSpan<const TextIndex> word_breaks() const override { return SkSpan<const TextIndex>(fWordBreaks); }
    SkSpan<const LineBreakOpportunity> line_breaks() const override { return SkSpan<const LineBreakOpportunity>(fLineBreaks); }

    bool isWhitespace(TextIndex offset) const override {
        SkUnichar u = getUnicharAt(offset);
        return fUnicode ? fUnicode->isWhitespace(u) : (u == ' ' || u == '\t' || u == '\n' || u == '\r');
    }

    bool isSpace(TextIndex offset) const override {
        SkUnichar u = getUnicharAt(offset);
        return fUnicode ? fUnicode->isSpace(u) : (u == ' ');
    }

    bool isTabulation(TextIndex offset) const override {
        SkUnichar u = getUnicharAt(offset);
        return u == '\t';
    }

    bool isHardLineBreak(TextIndex offset) const override {
        SkUnichar u = getUnicharAt(offset);
        return fUnicode ? fUnicode->isHardBreak(u) : (u == '\n' || u == '\r');
    }

    bool isControl(TextIndex offset) const override {
        SkUnichar u = getUnicharAt(offset);
        return fUnicode ? fUnicode->isControl(u) : (u < 32 || (u >= 0x7F && u <= 0x9F));
    }

    void reorderVisual(
        SkSpan<const uint8_t> run_levels,
        SkSpan<int32_t> out_visual_to_logical) const override
    {
        SkASSERT(run_levels.size() == out_visual_to_logical.size());
        if (run_levels.empty()) {
            return;
        }
        if (fUnicode) {
            fUnicode->reorderVisual(run_levels.data(), run_levels.size(), out_visual_to_logical.data());
        } else {
            // Fallback identity mapping
            for (size_t i = 0; i < run_levels.size(); ++i) {
                out_visual_to_logical[i] = i;
            }
        }
    }

private:
    SkUnichar getUnicharAt(TextIndex offset) const {
        if (offset.value >= fText.size()) {
            return 0;
        }
        const char* ptr = fText.data() + offset.value;
        const char* end = fText.data() + fText.size();
        return SkUTF::NextUTF8(&ptr, end);
    }

    void init(SkSpan<const StyleSpan> styles) {
        if (!fUnicode || fText.empty()) {
            // Fallback single run if text is empty or unicode shaper absent
            ItemizedRun run;
            run.text_range = TextRange(TextIndex(0), TextIndex(fText.size()));
            if (!styles.empty()) {
                run.font = styles[0].font;
            }
            fRuns.push_back(run);
            fGraphemeBreaks.push_back(TextIndex(0));
            fGraphemeBreaks.push_back(TextIndex(fText.size()));
            fWordBreaks.push_back(TextIndex(0));
            fWordBreaks.push_back(TextIndex(fText.size()));
            return;
        }

        // 1. Compute BiDi regions
        std::vector<SkUnicode::BidiRegion> bidiRegions;
        fUnicode->getBidiRegions(fText.data(), fText.size(), SkUnicode::TextDirection::kLTR, &bidiRegions);
        if (bidiRegions.empty()) {
            bidiRegions.emplace_back(0, fText.size(), 0);
        }

        for (const auto& bidi : bidiRegions) {
            ItemizedRun run;
            run.text_range = TextRange(TextIndex(bidi.start), TextIndex(bidi.end));
            run.bidi_level = bidi.level;
            run.direction = (bidi.level % 2 == 0) ? Direction::kLTR : Direction::kRTL;
            // Match font from styles span
            for (const auto& style : styles) {
                if (style.range.contains(TextIndex(bidi.start))) {
                    run.font = style.font;
                    break;
                }
            }
            if (styles.empty()) {
                run.font = SkFont();
            } else if (!run.font.getTypeface()) {
                run.font = styles[0].font;
            }
            fRuns.push_back(run);
        }

        // 2. Compute Grapheme breaks
        auto graphemeIter = fUnicode->makeBreakIterator(SkUnicode::BreakType::kGraphemes);
        if (graphemeIter) {
            graphemeIter->setText(fText.data(), fText.size());
            for (auto pos = graphemeIter->first(); !graphemeIter->isDone(); pos = graphemeIter->next()) {
                fGraphemeBreaks.push_back(TextIndex(pos));
            }
        }

        // 3. Compute Word breaks
        std::vector<SkUnicode::Position> words;
        fUnicode->getUtf8Words(fText.data(), fText.size(), nullptr, &words);
        for (auto pos : words) {
            fWordBreaks.push_back(TextIndex(pos));
        }

        // 4. Compute Line Break opportunities
        auto lineIter = fUnicode->makeBreakIterator(SkUnicode::BreakType::kLines);
        if (lineIter) {
            lineIter->setText(fText.data(), fText.size());
            for (auto pos = lineIter->first(); !lineIter->isDone(); pos = lineIter->next()) {
                LineBreakOpportunity lb;
                lb.offset = TextIndex(pos);
                lb.is_hard_break = (pos > 0 && isHardLineBreak(TextIndex(pos - 1)));
                fLineBreaks.push_back(lb);
            }
        }
    }

    std::string fText;
    std::unique_ptr<SkUnicode> fUnicode;
    std::vector<ItemizedRun> fRuns;
    std::vector<TextIndex> fGraphemeBreaks;
    std::vector<TextIndex> fWordBreaks;
    std::vector<LineBreakOpportunity> fLineBreaks;
};

} // namespace

std::unique_ptr<const UnicodeParagraph> UnicodeParagraph::Make(
    std::string_view utf8_text,
    SkSpan<const StyleSpan> styles)
{
    return std::make_unique<UnicodeParagraphImpl>(utf8_text, styles);
}

} // namespace skia::text_editor
