/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/TextEditorController.h"
#include "src/base/SkUTF.h"
#include <algorithm>

namespace skia::text_editor {

namespace {

class TextEditorControllerImpl : public TextEditorController {
public:
    TextEditorControllerImpl(
        std::string initial_text,
        SkFont default_font,
        SkColor4f text_color,
        LayoutConstraints constraints)
        : fText(std::move(initial_text))
        , fDefaultFont(std::move(default_font))
        , fTextColor(text_color)
        , fConstraints(constraints)
    {
        rebuildPipeline();
        // Initially place cursor at beginning
        CaretPosition startPos;
        startPos.text_index = TextIndex(0);
        startPos.affinity = Affinity::kDownstream;
        if (fSpatialIndex && !fSpatialIndex->formatted().lines().empty()) {
            startPos = fSpatialIndex->hitTest(0.0f, 0.0f);
        }
        fSelection.anchor = startPos;
        fSelection.focus = startPos;
    }

    std::string_view text() const override { return fText; }
    const EditorSelection& selection() const override { return fSelection; }
    const ParagraphSpatialIndex& spatial_index() const override { return *fSpatialIndex; }
    const LayoutConstraints& constraints() const override { return fConstraints; }

    void insertText(std::string_view utf8_text) override {
        if (!fSelection.is_collapsed()) {
            TextRange range = fSelection.text_range();
            size_t start = range.start.value;
            size_t len = range.length();
            fText.replace(start, len, utf8_text);
            rebuildPipeline();
            size_t newCursor = start + utf8_text.size();
            updateCursorPosition(newCursor);
        } else {
            size_t pos = std::min(fSelection.focus.text_index.value, fText.size());
            fText.insert(pos, utf8_text);
            rebuildPipeline();
            size_t newCursor = pos + utf8_text.size();
            updateCursorPosition(newCursor);
        }
    }

    void deleteBackward() override {
        if (!fSelection.is_collapsed()) {
            TextRange range = fSelection.text_range();
            size_t start = range.start.value;
            size_t len = range.length();
            fText.erase(start, len);
            rebuildPipeline();
            updateCursorPosition(start);
        } else {
            size_t cursor = fSelection.focus.text_index.value;
            if (cursor == 0 || fText.empty()) {
                return;
            }
            // Step backward one UTF-8 codepoint
            size_t prevPos = cursor;
            while (prevPos > 0) {
                --prevPos;
                if ((static_cast<uint8_t>(fText[prevPos]) & 0xC0) != 0x80) {
                    break;
                }
            }
            fText.erase(prevPos, cursor - prevPos);
            rebuildPipeline();
            updateCursorPosition(prevPos);
        }
    }

    void deleteForward() override {
        if (!fSelection.is_collapsed()) {
            TextRange range = fSelection.text_range();
            size_t start = range.start.value;
            size_t len = range.length();
            fText.erase(start, len);
            rebuildPipeline();
            updateCursorPosition(start);
        } else {
            size_t cursor = fSelection.focus.text_index.value;
            if (cursor >= fText.size()) {
                return;
            }
            const char* begin = fText.data();
            const char* ptr = begin + cursor;
            const char* end = begin + fText.size();
            SkUTF::NextUTF8(&ptr, end);
            size_t nextPos = ptr - begin;
            fText.erase(cursor, nextPos - cursor);
            rebuildPipeline();
            updateCursorPosition(cursor);
        }
    }

    void setSelection(CaretPosition anchor, CaretPosition focus) override {
        fSelection.anchor = anchor;
        fSelection.focus = focus;
    }

    void collapseTo(CaretPosition pos) override {
        fSelection.anchor = pos;
        fSelection.focus = pos;
    }

    void selectAll() override {
        if (fText.empty()) {
            return;
        }
        CaretPosition start = fSpatialIndex->hitTest(0.0f, 0.0f);
        start.text_index = TextIndex(0);
        start.affinity = Affinity::kDownstream;

        CaretPosition end;
        end.text_index = TextIndex(fText.size());
        end.affinity = Affinity::kUpstream;
        if (!fSpatialIndex->formatted().lines().empty()) {
            const auto& lastLine = fSpatialIndex->formatted().lines().back();
            end.caret_rect = SkRect::MakeXYWH(lastLine.bounds.fRight, lastLine.baseline + lastLine.ascent,
                                              1.0f, std::abs(lastLine.ascent) + std::abs(lastLine.descent));
        }
        fSelection.anchor = start;
        fSelection.focus = end;
    }

    void moveCaret(CursorDirection dir, MovementGranularity gran, NavigationMode mode, bool select) override {
        CaretPosition next = fSpatialIndex->moveCaret(fSelection.focus, dir, gran, mode);
        fSelection.focus = next;
        if (!select) {
            fSelection.anchor = next;
        }
    }

    void moveCaretToPoint(SkScalar x, SkScalar y, bool select) override {
        CaretPosition hit = fSpatialIndex->hitTest(x, y);
        fSelection.focus = hit;
        if (!select) {
            fSelection.anchor = hit;
        }
    }

    void selectWordAtPoint(SkScalar x, SkScalar y) override {
        CaretPosition hit = fSpatialIndex->hitTest(x, y);
        TextRange wr = fSpatialIndex->getWordBoundary(hit.text_index);
        CaretPosition anchor;
        anchor.text_index = wr.start;
        anchor.affinity = Affinity::kDownstream;

        CaretPosition focus;
        focus.text_index = wr.end;
        focus.affinity = Affinity::kUpstream;

        fSelection.anchor = anchor;
        fSelection.focus = focus;
    }

    void setConstraints(LayoutConstraints constraints) override {
        fConstraints = constraints;
        rebuildPipeline();
        updateCursorPosition(fSelection.focus.text_index.value);
    }

private:
    void rebuildPipeline() {
        std::vector<StyleSpan> styles = {
            { TextRange(TextIndex(0), TextIndex(fText.size())), fDefaultFont, fTextColor }
        };
        auto unicodePara = UnicodeParagraph::Make(fText, styles);
        auto shapedPara = ShapedParagraph::Make(std::move(unicodePara));
        auto formattedPara = FormattedParagraph::Make(std::move(shapedPara), fConstraints);
        fSpatialIndex = ParagraphSpatialIndex::Make(std::move(formattedPara));
    }

    void updateCursorPosition(size_t index) {
        index = std::min(index, fText.size());
        CaretPosition pos;
        pos.text_index = TextIndex(index);
        pos.affinity = (index == fText.size()) ? Affinity::kUpstream : Affinity::kDownstream;

        // Position rect lookup
        if (fSpatialIndex) {
            const auto& lines = fSpatialIndex->formatted().lines();
            if (!lines.empty()) {
                pos.caret_rect = SkRect::MakeXYWH(lines[0].bounds.fLeft, lines[0].baseline + lines[0].ascent,
                                                  1.0f, std::abs(lines[0].ascent) + std::abs(lines[0].descent));
            }
        }
        fSelection.anchor = pos;
        fSelection.focus = pos;
    }

    std::string fText;
    SkFont fDefaultFont;
    SkColor4f fTextColor;
    LayoutConstraints fConstraints;
    EditorSelection fSelection;
    std::unique_ptr<const ParagraphSpatialIndex> fSpatialIndex;
};

} // namespace

std::unique_ptr<TextEditorController> TextEditorController::Make(
    std::string initial_text,
    SkFont default_font,
    SkColor4f text_color,
    LayoutConstraints constraints)
{
    return std::make_unique<TextEditorControllerImpl>(
        std::move(initial_text),
        std::move(default_font),
        text_color,
        constraints);
}

} // namespace skia::text_editor
