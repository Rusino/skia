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

    EditorSelection selBefore = fSelection;
    std::string textBefore;
    size_t insertPos = 0;
    EditCommand::Kind kind = EditCommand::Kind::kTyping;

    if (!fSelection.is_collapsed()) {
        kind = EditCommand::Kind::kCutOrBlock;
        textBefore = copySelection();
        if (!fSelection.ranges.empty()) {
            insertPos = fSelection.ranges.front().start.value;
            for (auto it = fSelection.ranges.rbegin(); it != fSelection.ranges.rend(); ++it) {
                fDocument->erase(*it);
            }
            fSelection.ranges.clear();
            fDocument->insert(TextIndex(insertPos), sanitized);
            size_t finalCaret = insertPos + sanitized.size();
            updateCursorPosition(finalCaret);
            CaretPosition accuratePos = fDocument->spatial_index().moveCaret(
                fSelection.focus, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);
            if (accuratePos.text_index.value == finalCaret) {
                fSelection.collapse_to(accuratePos);
            }
        } else {
            TextRange range = fSelection.text_range();
            insertPos = range.start.value;
            fDocument->replace(range, sanitized);
            size_t finalCaret = insertPos + sanitized.size();
            updateCursorPosition(finalCaret);
            CaretPosition accuratePos = fDocument->spatial_index().moveCaret(
                fSelection.focus, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);
            if (accuratePos.text_index.value == finalCaret) {
                fSelection.collapse_to(accuratePos);
            }
        }
    } else {
        insertPos = std::min(fSelection.focus.text_index.value, fDocument->text().size());
        if (sanitized == "\n") {
            kind = EditCommand::Kind::kNewline;
        } else if (sanitized.size() > 4) {
            kind = EditCommand::Kind::kPaste;
        } else {
            kind = EditCommand::Kind::kTyping;
        }
        fDocument->insert(TextIndex(insertPos), sanitized);
        size_t finalCaret = insertPos + sanitized.size();
        updateCursorPosition(finalCaret);
        CaretPosition accuratePos = fDocument->spatial_index().moveCaret(
            fSelection.focus, CursorDirection::kRight, MovementGranularity::kGrapheme, NavigationMode::kTextLogical);
        if (accuratePos.text_index.value == finalCaret) {
            fSelection.collapse_to(accuratePos);
        }
    }

    if (!fIsPerformingUndoRedo) {
        EditCommand cmd;
        cmd.kind = kind;
        cmd.position = TextIndex(insertPos);
        cmd.textBefore = std::move(textBefore);
        cmd.textAfter = sanitized;
        cmd.selectionBefore = selBefore;
        cmd.selectionAfter = fSelection;
        cmd.timestamp = std::chrono::steady_clock::now();
        pushEditCommand(std::move(cmd));
    }

    notifyRedraw();
}


void TextEditorViewModel::deleteBackward(MovementGranularity gran) {
    EditorSelection selBefore = fSelection;
    std::string textBefore;
    size_t deletePos = 0;

    if (!fSelection.is_collapsed()) {
        textBefore = copySelection();
        if (!fSelection.ranges.empty()) {
            deletePos = fSelection.ranges.front().start.value;
            for (auto it = fSelection.ranges.rbegin(); it != fSelection.ranges.rend(); ++it) {
                fDocument->erase(*it);
            }
            fSelection.ranges.clear();
            updateCursorPosition(deletePos);
        } else {
            TextRange range = fSelection.text_range();
            deletePos = range.start.value;
            fDocument->erase(range);
            updateCursorPosition(deletePos);
        }

        if (!fIsPerformingUndoRedo) {
            EditCommand cmd;
            cmd.kind = EditCommand::Kind::kCutOrBlock;
            cmd.position = TextIndex(deletePos);
            cmd.textBefore = std::move(textBefore);
            cmd.textAfter = "";
            cmd.selectionBefore = selBefore;
            cmd.selectionAfter = fSelection;
            cmd.timestamp = std::chrono::steady_clock::now();
            pushEditCommand(std::move(cmd));
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
        deletePos = prevPos;
        textBefore = std::string(text.substr(prevPos, cursor - prevPos));
        fDocument->erase(TextRange(TextIndex(prevPos), TextIndex(cursor)));
        updateCursorPosition(prevPos);

        if (!fIsPerformingUndoRedo) {
            EditCommand cmd;
            cmd.kind = EditCommand::Kind::kDelete;
            cmd.position = TextIndex(deletePos);
            cmd.textBefore = std::move(textBefore);
            cmd.textAfter = "";
            cmd.selectionBefore = selBefore;
            cmd.selectionAfter = fSelection;
            cmd.timestamp = std::chrono::steady_clock::now();
            pushEditCommand(std::move(cmd));
        }
    }
    notifyRedraw();
}

void TextEditorViewModel::deleteForward(MovementGranularity gran) {
    if (!fSelection.is_collapsed()) {
        deleteBackward(gran);
        return;
    }
    size_t cursor = fSelection.focus.text_index.value;
    std::string_view text = fDocument->text();
    if (cursor >= text.size()) {
        return;
    }
    EditorSelection selBefore = fSelection;
    const char* begin = text.data();
    const char* ptr = begin + cursor;
    const char* end = begin + text.size();
    SkUTF::NextUTF8(&ptr, end);
    size_t nextPos = ptr - begin;
    std::string textBefore = std::string(text.substr(cursor, nextPos - cursor));
    fDocument->erase(TextRange(TextIndex(cursor), TextIndex(nextPos)));
    updateCursorPosition(cursor);

    if (!fIsPerformingUndoRedo) {
        EditCommand cmd;
        cmd.kind = EditCommand::Kind::kDelete;
        cmd.position = TextIndex(cursor);
        cmd.textBefore = std::move(textBefore);
        cmd.textAfter = "";
        cmd.selectionBefore = selBefore;
        cmd.selectionAfter = fSelection;
        cmd.timestamp = std::chrono::steady_clock::now();
        pushEditCommand(std::move(cmd));
    }
    notifyRedraw();
}

void TextEditorViewModel::moveCaret(CursorDirection dir, MovementGranularity gran, NavigationMode mode, bool select) {
    CaretPosition next = fDocument->spatial_index().moveCaret(fSelection.focus, dir, gran, mode);
    if (!select) {
        fSelection.collapse_to(next);
    } else {
        fSelection.set_span(fSelection.anchor, next);
    }
    notifyRedraw();
}

void TextEditorViewModel::moveCaretToPoint(SkScalar screenX, SkScalar screenY, bool select) {
    SkScalar docX = screenX + fScrollOffset.fX;
    SkScalar docY = screenY + fScrollOffset.fY;
    CaretPosition hit = fDocument->spatial_index().hitTest(docX, docY);
    if (!select) {
        fSelection.collapse_to(hit);
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
        fSelection.set_ranges(fSelection.anchor, hit, std::move(visualRanges));
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
    fSelection.set_span(start, end);
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

    fSelection.set_span(anchor, focus);
    notifyRedraw();
}

void TextEditorViewModel::setSelection(CaretPosition anchor, CaretPosition focus) {
    fSelection.set_span(anchor, focus);
    notifyRedraw();
}

void TextEditorViewModel::collapseTo(CaretPosition pos) {
    fSelection.collapse_to(pos);
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
    if (key == skui::Key::kCtrl) {
        fCtrlKeyHeld = (state == skui::InputState::kDown);
        return true;
    }

    if (state != skui::InputState::kDown) {
        return false;
    }

    bool shift = (modifiers & skui::ModifierKey::kShift) != skui::ModifierKey::kNone;
    bool ctrlOrCmd = ((modifiers & (skui::ModifierKey::kControl | skui::ModifierKey::kCommand)) != skui::ModifierKey::kNone) || fCtrlKeyHeld;

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
        case skui::Key::kC:
            if (ctrlOrCmd) {
                if (fClipboardSetter) {
                    fClipboardSetter(copySelection());
                }
                return true;
            }
            break;
        case skui::Key::kX:
            if (ctrlOrCmd) {
                cutSelection();
                return true;
            }
            break;
        case skui::Key::kV:
            if (ctrlOrCmd) {
                if (fClipboardGetter) {
                    pasteText(fClipboardGetter());
                }
                return true;
            }
            break;
        case skui::Key::kZ:
            if (ctrlOrCmd) {
                if (shift) {
                    redo();
                } else {
                    undo();
                }
                return true;
            }
            break;
        case skui::Key::kY:
            if (ctrlOrCmd) {
                redo();
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
    bool ctrlOrCmd = ((modifiers & (skui::ModifierKey::kControl | skui::ModifierKey::kCommand)) != skui::ModifierKey::kNone) || fCtrlKeyHeld;

    // Hostile Invariant: Never insert text or control characters when Control/Command is held
    // or when raw ASCII control characters (c < 32) arrive.
    if (ctrlOrCmd || (c < 32 && c != '\n' && c != '\t')) {
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
    fSelection.collapse_to(pos);
}

void TextEditorViewModel::setClipboardHandlers(ClipboardSetter setter, ClipboardGetter getter) {
    fClipboardSetter = std::move(setter);
    fClipboardGetter = std::move(getter);
}

std::string TextEditorViewModel::copySelection() const {
    if (fSelection.is_collapsed()) {
        return "";
    }
    std::string_view docText = fDocument->text();
    if (!fSelection.ranges.empty()) {
        std::string result;
        for (const auto& r : fSelection.ranges) {
            size_t start = std::min(r.start.value, docText.size());
            size_t end = std::min(r.end.value, docText.size());
            if (start > end) {
                std::swap(start, end);
            }
            result.append(docText.substr(start, end - start));
        }
        return result;
    }
    TextRange range = fSelection.text_range();
    size_t start = std::min(range.start.value, docText.size());
    size_t end = std::min(range.end.value, docText.size());
    if (start > end) {
        std::swap(start, end);
    }
    return std::string(docText.substr(start, end - start));
}

void TextEditorViewModel::cutSelection() {
    if (fSelection.is_collapsed()) {
        return;
    }
    std::string copied = copySelection();
    if (fClipboardSetter) {
        fClipboardSetter(copied);
    }
    deleteBackward();
}

void TextEditorViewModel::pasteText(std::string_view raw) {
    if (raw.empty()) {
        return;
    }
    // Smart Duplicate Invariant:
    // If the active selection is non-collapsed and its content is identical to the pasted payload,
    // collapse the selection to its end (right edge) and insert the text adjacent to it.
    // This allows immediate duplication via Ctrl+C -> Ctrl+V without requiring manual cursor movement!
    if (!fSelection.is_collapsed() && raw == copySelection()) {
        size_t rightEdge = 0;
        if (!fSelection.ranges.empty()) {
            for (const auto& r : fSelection.ranges) {
                rightEdge = std::max({rightEdge, r.start.value, r.end.value});
            }
        } else {
            TextRange r = fSelection.text_range();
            rightEdge = std::max(r.start.value, r.end.value);
        }
        updateCursorPosition(rightEdge);
        collapseTo(fSelection.focus);
    }
    insertText(raw);
}

bool TextEditorViewModel::canUndo() const {
    return !fUndoStack.empty();
}

bool TextEditorViewModel::canRedo() const {
    return !fRedoStack.empty();
}

bool TextEditorViewModel::undo() {
    if (fUndoStack.empty()) {
        return false;
    }

    EditCommand cmd = std::move(fUndoStack.back());
    fUndoStack.pop_back();

    fIsPerformingUndoRedo = true;

    // Apply inverse mutation:
    // 1. If text was inserted, erase it
    if (!cmd.textAfter.empty()) {
        fDocument->erase(TextRange(cmd.position, cmd.position + cmd.textAfter.size()));
    }
    // 2. If text was deleted, re-insert it
    if (!cmd.textBefore.empty()) {
        fDocument->insert(cmd.position, cmd.textBefore);
    }

    // 3. Restore selection state
    if (cmd.selectionBefore.is_collapsed()) {
        updateCursorPosition(cmd.selectionBefore.focus.text_index.value);
    } else {
        fSelection = cmd.selectionBefore;
    }

    fIsPerformingUndoRedo = false;
    fRedoStack.push_back(std::move(cmd));

    notifyRedraw();
    return true;
}

bool TextEditorViewModel::redo() {
    if (fRedoStack.empty()) {
        return false;
    }

    EditCommand cmd = std::move(fRedoStack.back());
    fRedoStack.pop_back();

    fIsPerformingUndoRedo = true;

    // Apply forward mutation:
    if (!cmd.textBefore.empty()) {
        fDocument->erase(TextRange(cmd.position, cmd.position + cmd.textBefore.size()));
    }
    if (!cmd.textAfter.empty()) {
        fDocument->insert(cmd.position, cmd.textAfter);
    }

    if (cmd.selectionAfter.is_collapsed()) {
        updateCursorPosition(cmd.selectionAfter.focus.text_index.value);
    } else {
        fSelection = cmd.selectionAfter;
    }

    fIsPerformingUndoRedo = false;
    fUndoStack.push_back(std::move(cmd));

    notifyRedraw();
    return true;
}

void TextEditorViewModel::clearHistory() {
    fUndoStack.clear();
    fRedoStack.clear();
}

void TextEditorViewModel::pushEditCommand(EditCommand cmd) {
    if (fIsPerformingUndoRedo) {
        return;
    }

    // Any new mutation invalidates the entire Redo stack
    fRedoStack.clear();

    auto now = cmd.timestamp;
    bool coalesced = false;

    if (!fUndoStack.empty()) {
        auto& last = fUndoStack.back();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - last.timestamp).count();

        // 1. Typing coalescing
        if (last.kind == EditCommand::Kind::kTyping && cmd.kind == EditCommand::Kind::kTyping) {
            bool isBoundary = (cmd.textAfter == " " || cmd.textAfter == "\t" ||
                               std::ispunct(static_cast<unsigned char>(cmd.textAfter[0])));
            bool lastWasBoundary = (last.textAfter == " " || last.textAfter == "\t" ||
                                    (last.textAfter.size() == 1 && std::ispunct(static_cast<unsigned char>(last.textAfter[0]))));

            if (dt <= 750 &&
                cmd.position.value == (last.position.value + last.textAfter.size()) &&
                !isBoundary && !lastWasBoundary)
            {
                last.textAfter.append(cmd.textAfter);
                last.selectionAfter = cmd.selectionAfter;
                last.timestamp = now;
                coalesced = true;
            }
        }
        // 2. Backward deletion coalescing
        else if (last.kind == EditCommand::Kind::kDelete && cmd.kind == EditCommand::Kind::kDelete) {
            if (dt <= 750 && (cmd.position.value + cmd.textBefore.size() == last.position.value)) {
                last.textBefore = cmd.textBefore + last.textBefore;
                last.position = cmd.position;
                last.selectionAfter = cmd.selectionAfter;
                last.timestamp = now;
                coalesced = true;
            }
        }
    }

    if (!coalesced) {
        fUndoStack.push_back(std::move(cmd));
        if (fUndoStack.size() > kMaxUndoDepth) {
            fUndoStack.pop_front();
        }
    }
}

} // namespace skia::text_editor
