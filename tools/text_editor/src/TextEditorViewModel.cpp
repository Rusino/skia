/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "tools/text_editor/include/TextEditorViewModel.h"
#include "include/core/SkFontMetrics.h"
#include "src/base/SkUTF.h"
#include <algorithm>
#include <cmath>

namespace skia::text_editor {

TextEditorViewModel::TextEditorViewModel(
    std::unique_ptr<TextDocument> document,
    NavigationMode default_nav_mode)
    : fDocument(std::move(document))
    , fNavMode(default_nav_mode)
{
    updateCursorPosition(0);
}

TextEditorViewModel::TextEditorViewModel(
    std::string initial_text,
    SkFont default_font,
    SkColor4f text_color,
    LayoutConstraints constraints,
    NavigationMode default_nav_mode)
    : fDocument(std::make_unique<TextDocument>(
          std::move(initial_text),
          std::move(default_font),
          text_color,
          std::move(constraints)))
    , fNavMode(default_nav_mode)
{
    updateCursorPosition(0);
}

namespace {

// Sanitizes input text per Domain Invariant 14:
// - Normalizes \r\n and single \r to \n
// - Expands \t to 4 soft spaces
// - Drops ASCII C0 (< 32, except \n), DEL (127), and binary nulls
// - Preserves all valid UTF-8 sequences including Unicode Format Controls (Cf, like ZWJ/ZWNJ/LRM/RLM)
std::string SanitizeInputText(std::string_view raw) {
    std::string sanitized;
    sanitized.reserve(raw.size());

    const char* ptr = raw.data();
    const char* end = ptr + raw.size();

    while (ptr < end) {
        // Check for CRLF or CR
        if (*ptr == '\r') {
            ptr++;
            if (ptr < end && *ptr == '\n') {
                ptr++;
            }
            sanitized.push_back('\n');
            continue;
        }

        // Check for Tab
        if (*ptr == '\t') {
            sanitized.append("    ");
            ptr++;
            continue;
        }

        // Check single-byte ASCII
        unsigned char byte = static_cast<unsigned char>(*ptr);
        if (byte < 0x80) {
            if (byte == '\n' || byte >= 32) {
                if (byte != 0x7F) { // Exclude DEL
                    sanitized.push_back(static_cast<char>(byte));
                }
            }
            ptr++;
            continue;
        }

        // Multi-byte UTF-8 sequence
        const char* prev = ptr;
        SkUnichar u = SkUTF::NextUTF8(&ptr, end);
        if (u < 0) {
            // Invalid UTF-8 sequence, skip bad byte
            ptr = prev + 1;
            continue;
        }

        // Exclude Unicode C1 controls (0x80 - 0x9F)
        if (u >= 0x80 && u <= 0x9F) {
            continue;
        }

        // Append valid multi-byte sequence (includes Cf category like ZWJ/ZWNJ/LRM/RLM)
        sanitized.append(prev, ptr - prev);
    }

    return sanitized;
}

} // namespace

void TextEditorViewModel::insertText(std::string_view utf8_text) {
    std::string sanitized = SanitizeInputText(utf8_text);
    if (sanitized.empty()) {
        return;
    }

    if (!fSelection.is_collapsed()) {
        if (!fSelection.ranges.empty()) {
            // Delete ranges in reverse order
            for (auto it = fSelection.ranges.rbegin(); it != fSelection.ranges.rend(); ++it) {
                fDocument->erase(*it);
            }
            size_t insertPos = fSelection.ranges.front().start.value;
            fSelection.ranges.clear();
            fDocument->insert(TextIndex(insertPos), sanitized);
            // Move caret via spatial index hit-test/moveCaret to ensure accurate multi-line caret_rect
            size_t finalCaret = insertPos + sanitized.size();
            updateCursorPosition(finalCaret);
            CaretPosition accuratePos = fDocument->spatial_index().moveCaret(
                fSelection.focus, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);
            if (accuratePos.text_index.value == finalCaret) {
                fSelection.anchor = accuratePos;
                fSelection.focus = accuratePos;
            }
        } else {
            TextRange range = fSelection.text_range();
            size_t start = range.start.value;
            fDocument->replace(range, sanitized);
            size_t finalCaret = start + sanitized.size();
            updateCursorPosition(finalCaret);
            CaretPosition accuratePos = fDocument->spatial_index().moveCaret(
                fSelection.focus, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);
            if (accuratePos.text_index.value == finalCaret) {
                fSelection.anchor = accuratePos;
                fSelection.focus = accuratePos;
            }
        }
    } else {
        size_t pos = std::min(fSelection.focus.text_index.value, fDocument->text().size());
        fDocument->insert(TextIndex(pos), sanitized);
        size_t finalCaret = pos + sanitized.size();
        updateCursorPosition(finalCaret);
        CaretPosition accuratePos = fDocument->spatial_index().moveCaret(
            fSelection.focus, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);
        if (accuratePos.text_index.value == finalCaret) {
            fSelection.anchor = accuratePos;
            fSelection.focus = accuratePos;
        }
    }
    notifyRedraw();
}


void TextEditorViewModel::deleteBackward(MovementGranularity gran) {
    if (!fSelection.is_collapsed()) {
        if (!fSelection.ranges.empty()) {
            size_t targetCaret = fSelection.ranges.front().start.value;
            for (auto it = fSelection.ranges.rbegin(); it != fSelection.ranges.rend(); ++it) {
                fDocument->erase(*it);
            }
            fSelection.ranges.clear();
            updateCursorPosition(targetCaret);
        } else {
            TextRange range = fSelection.text_range();
            size_t start = range.start.value;
            fDocument->erase(range);
            updateCursorPosition(start);
        }
    } else {
        size_t cursor = fSelection.focus.text_index.value;
        std::string_view text = fDocument->text();
        if (cursor == 0 || text.empty()) {
            return;
        }
        size_t prevPos = cursor;
        while (prevPos > 0) {
            --prevPos;
            if ((static_cast<uint8_t>(text[prevPos]) & 0xC0) != 0x80) {
                break;
            }
        }
        fDocument->erase(TextRange(TextIndex(prevPos), TextIndex(cursor)));
        updateCursorPosition(prevPos);
    }
    notifyRedraw();
}

void TextEditorViewModel::deleteForward(MovementGranularity gran) {
    if (!fSelection.is_collapsed()) {
        if (!fSelection.ranges.empty()) {
            size_t targetCaret = fSelection.ranges.front().start.value;
            for (auto it = fSelection.ranges.rbegin(); it != fSelection.ranges.rend(); ++it) {
                fDocument->erase(*it);
            }
            fSelection.ranges.clear();
            updateCursorPosition(targetCaret);
        } else {
            TextRange range = fSelection.text_range();
            size_t start = range.start.value;
            fDocument->erase(range);
            updateCursorPosition(start);
        }
    } else {
        size_t cursor = fSelection.focus.text_index.value;
        std::string_view text = fDocument->text();
        if (cursor >= text.size()) {
            return;
        }
        const char* begin = text.data();
        const char* ptr = begin + cursor;
        const char* end = begin + text.size();
        SkUTF::NextUTF8(&ptr, end);
        size_t nextPos = ptr - begin;
        fDocument->erase(TextRange(TextIndex(cursor), TextIndex(nextPos)));
        updateCursorPosition(cursor);
    }
    notifyRedraw();
}

void TextEditorViewModel::moveCaret(CursorDirection dir, MovementGranularity gran, NavigationMode mode, bool select) {
    CaretPosition next = fDocument->spatial_index().moveCaret(fSelection.focus, dir, gran, mode);
    fSelection.focus = next;
    if (!select) {
        fSelection.anchor = next;
    }
    notifyRedraw();
}

void TextEditorViewModel::moveCaretToPoint(SkScalar screenX, SkScalar screenY, bool select) {
    SkScalar docX = screenX + fScrollOffset.fX;
    SkScalar docY = screenY + fScrollOffset.fY;
    CaretPosition hit = fDocument->spatial_index().hitTest(docX, docY);
    fSelection.focus = hit;
    if (!select) {
        fSelection.anchor = hit;
        fSelection.ranges.clear();
        fDragAnchorDocPoint = SkPoint::Make(docX, docY);
        fHasDragPoint = true;
    } else {
        if (!fHasDragPoint) {
            fDragAnchorDocPoint = SkPoint::Make(docX, docY);
            fHasDragPoint = true;
        }
        std::vector<SkRect> visualRects;
        std::vector<TextRange> visualRanges;
        fDocument->spatial_index().getSelectionForVisualDrag(
            fDragAnchorDocPoint.fX, fDragAnchorDocPoint.fY,
            docX, docY,
            visualRects, visualRanges);
        fSelection.ranges = std::move(visualRanges);
    }
    notifyRedraw();
}

void TextEditorViewModel::selectAll() {
    std::string_view text = fDocument->text();
    if (text.empty()) {
        return;
    }
    CaretPosition start = fDocument->spatial_index().hitTest(0.0f, 0.0f);
    start.text_index = TextIndex(0);
    start.affinity = Affinity::kDownstream;

    CaretPosition end;
    end.text_index = TextIndex(text.size());
    end.affinity = Affinity::kUpstream;
    const auto& lines = fDocument->formatted().lines();
    if (!lines.empty()) {
        const auto& lastLine = lines.back();
        end.caret_rect = SkRect::MakeXYWH(lastLine.bounds.fRight, lastLine.baseline + lastLine.ascent,
                                          1.0f, std::abs(lastLine.ascent) + std::abs(lastLine.descent));
    }
    fSelection.anchor = start;
    fSelection.focus = end;
    notifyRedraw();
}

void TextEditorViewModel::selectWordAtPoint(SkScalar screenX, SkScalar screenY) {
    SkScalar docX = screenX + fScrollOffset.fX;
    SkScalar docY = screenY + fScrollOffset.fY;
    CaretPosition hit = fDocument->spatial_index().hitTest(docX, docY);
    TextRange wr = fDocument->spatial_index().getWordBoundary(hit.text_index);

    CaretPosition anchor;
    anchor.text_index = wr.start;
    anchor.affinity = Affinity::kDownstream;

    CaretPosition focus;
    focus.text_index = wr.end;
    focus.affinity = Affinity::kUpstream;

    fSelection.anchor = anchor;
    fSelection.focus = focus;
    notifyRedraw();
}

void TextEditorViewModel::setSelection(CaretPosition anchor, CaretPosition focus) {
    fSelection.anchor = anchor;
    fSelection.focus = focus;
    notifyRedraw();
}

void TextEditorViewModel::collapseTo(CaretPosition pos) {
    fSelection.anchor = pos;
    fSelection.focus = pos;
    notifyRedraw();
}

void TextEditorViewModel::ensureCaretVisible(const SkRect& viewportBounds) {
    SkRect caret = screenCaretRect();
    if (caret.isEmpty() || viewportBounds.isEmpty()) {
        return;
    }

    if (caret.fLeft < viewportBounds.fLeft) {
        fScrollOffset.fX -= (viewportBounds.fLeft - caret.fLeft);
    } else if (caret.fRight > viewportBounds.fRight) {
        fScrollOffset.fX += (caret.fRight - viewportBounds.fRight);
    }

    if (caret.fTop < viewportBounds.fTop) {
        fScrollOffset.fY -= (viewportBounds.fTop - caret.fTop);
    } else if (caret.fBottom > viewportBounds.fBottom) {
        fScrollOffset.fY += (caret.fBottom - viewportBounds.fBottom);
    }
    notifyRedraw();
}

bool TextEditorViewModel::handleKey(skui::Key key, skui::InputState state, skui::ModifierKey modifiers) {
    if (state != skui::InputState::kDown) {
        return false;
    }

    bool shift = (modifiers & skui::ModifierKey::kShift) != skui::ModifierKey::kNone;
    bool ctrlOrCmd = ((modifiers & skui::ModifierKey::kControl) != skui::ModifierKey::kNone) ||
                     ((modifiers & skui::ModifierKey::kCommand) != skui::ModifierKey::kNone);

    switch (key) {
        case skui::Key::kLeft:
            moveCaret(CursorDirection::kLeft, MovementGranularity::kGrapheme, fNavMode, shift);
            return true;
        case skui::Key::kRight:
            moveCaret(CursorDirection::kRight, MovementGranularity::kGrapheme, fNavMode, shift);
            return true;
        case skui::Key::kUp:
            moveCaret(CursorDirection::kUp, MovementGranularity::kLine, NavigationMode::kScreenPhysical, shift);
            return true;
        case skui::Key::kDown:
            moveCaret(CursorDirection::kDown, MovementGranularity::kLine, NavigationMode::kScreenPhysical, shift);
            return true;
        case skui::Key::kBack:
            deleteBackward();
            return true;
        case skui::Key::kDelete:
            deleteForward();
            return true;
        case skui::Key::kA:
            if (ctrlOrCmd) {
                selectAll();
                return true;
            }
            break;
        case skui::Key::kOK:
            insertText("\n");
            return true;
        case skui::Key::kTab: {
            // Immediate Soft-Tab Normalization:
            // Calculate column offset from start of line
            std::string_view fullText = fDocument->text();
            size_t cursor = std::min(fSelection.focus.text_index.value, fullText.size());
            size_t lineStart = 0;
            if (cursor > 0) {
                size_t lastNewline = fullText.rfind('\n', cursor - 1);
                if (lastNewline != std::string_view::npos) {
                    lineStart = lastNewline + 1;
                }
            }
            // Count codepoints from lineStart to cursor
            size_t col = 0;
            const char* p = fullText.data() + lineStart;
            const char* end = fullText.data() + cursor;
            while (p < end) {
                SkUTF::NextUTF8(&p, end);
                col++;
            }
            constexpr size_t kTabSize = 4;
            size_t spacesNeeded = kTabSize - (col % kTabSize);
            std::string spaces(spacesNeeded, ' ');
            insertText(spaces);
            return true;
        }
        default:
            break;
    }
    return false;
}


bool TextEditorViewModel::handleChar(SkUnichar c, skui::ModifierKey modifiers) {
    if ((modifiers & (skui::ModifierKey::kControl | skui::ModifierKey::kCommand)) != skui::ModifierKey::kNone) {
        return false;
    }
    if (c < 32 && c != '\n' && c != '\t') {
        return false;
    }

    char utf8Buffer[4];
    size_t len = SkUTF::ToUTF8(c, utf8Buffer);
    if (len > 0) {
        insertText(std::string_view(utf8Buffer, len));
        return true;
    }
    return false;
}

SkRect TextEditorViewModel::screenCaretRect() const {
    SkRect r = fSelection.focus.caret_rect;
    r.offset(-fScrollOffset.fX, -fScrollOffset.fY);
    return r;
}

std::vector<SkRect> TextEditorViewModel::screenSelectionRects() const {
    std::vector<SkRect> rects;
    if (!fSelection.is_collapsed()) {
        if (!fSelection.ranges.empty()) {
            for (const auto& r : fSelection.ranges) {
                std::vector<SkRect> subRects;
                fDocument->spatial_index().getSelectionRects(r, subRects);
                rects.insert(rects.end(), subRects.begin(), subRects.end());
            }
        } else {
            fDocument->spatial_index().getSelectionRects(fSelection.text_range(), rects);
        }
        for (auto& r : rects) {
            r.offset(-fScrollOffset.fX, -fScrollOffset.fY);
        }
    }
    return rects;
}

void TextEditorViewModel::visitScreenRuns(const SkRect& screenClip, RenderRunVisitor visitor) const {
    SkRect docClip = screenClip;
    docClip.offset(fScrollOffset.fX, fScrollOffset.fY);
    fDocument->visitDocumentRuns(docClip, std::move(visitor));
}

void TextEditorViewModel::updateCursorPosition(size_t index) {
    std::string_view text = fDocument->text();
    index = std::min(index, text.size());
    CaretPosition pos;
    pos.text_index = TextIndex(index);
    pos.affinity = (index == text.size()) ? Affinity::kUpstream : Affinity::kDownstream;

    const auto& lines = fDocument->formatted().lines();
    if (!lines.empty()) {
        const auto* targetLine = &lines[0];
        for (const auto& line : lines) {
            if (index >= line.text_range.start.value && index < line.text_range.end.value) {
                targetLine = &line;
                break;
            }
        }
        if (index >= lines.back().text_range.end.value) {
            targetLine = &lines.back();
        }

        SkScalar caretTop = targetLine->baseline + targetLine->typographic_ascent;
        SkScalar caretHeight = std::abs(targetLine->typographic_ascent) + std::abs(targetLine->typographic_descent);
        if (caretHeight <= 0.0f) {
            caretHeight = 16.0f;
        }

        if (index == text.size()) {
            const auto& lastLine = lines.back();
            pos.caret_rect = SkRect::MakeXYWH(lastLine.bounds.fRight,
                                              lastLine.baseline + lastLine.typographic_ascent,
                                              1.0f,
                                              std::abs(lastLine.typographic_ascent) + std::abs(lastLine.typographic_descent));
        } else {
            std::vector<SkRect> rects;
            fDocument->spatial_index().getSelectionRects(TextRange(TextIndex(index), TextIndex(index + 1)), rects);
            if (!rects.empty()) {
                SkScalar h = rects[0].height() > 0.0f ? rects[0].height() : caretHeight;
                pos.caret_rect = SkRect::MakeXYWH(rects[0].fLeft, rects[0].fTop, 1.0f, h);
            } else {
                SkScalar x = (index > targetLine->text_range.start.value) ? targetLine->bounds.fRight : targetLine->bounds.fLeft;
                pos.caret_rect = SkRect::MakeXYWH(x, caretTop, 1.0f, caretHeight);
            }
        }
    }
    if (pos.caret_rect.isEmpty()) {
        pos.caret_rect = SkRect::MakeXYWH(0.0f, 0.0f, 1.0f, 16.0f);
    }
    fSelection.anchor = pos;
    fSelection.focus = pos;
}

} // namespace skia::text_editor
