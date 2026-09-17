/*
 * Copyright 2026 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkSurface.h"
#include "src/base/SkUTF.h"
#include "tools/fonts/FontToolUtils.h"
#include "tools/sk_app/Application.h"
#include "tools/sk_app/Window.h"
#include "tools/text_editor/include/TextEditorController.h"
#include "tools/text_editor/include/TextEditorPainter.h"
#include "tools/window/DisplayParams.h"

using namespace sk_app;
using namespace skia::text_editor;

class TextEditorApp : public Application, public Window::Layer {
public:
    TextEditorApp(int argc, char** argv, void* platformData)
        : fBackendType(Window::kRaster_BackendType)
        , fIsMouseDown(false)
    {
        SkGraphics::Init();

        fWindow = Window::CreateNativeWindow(platformData);
        fWindow->setRequestedDisplayParams(skwindow::DisplayParams());
        fWindow->pushLayer(this);

#if defined(SK_GL)
        if (!fWindow->attach(Window::kNativeGL_BackendType)) {
            fWindow->attach(Window::kRaster_BackendType);
            fBackendType = Window::kRaster_BackendType;
        } else {
            fBackendType = Window::kNativeGL_BackendType;
        }
#else
        fWindow->attach(Window::kRaster_BackendType);
        fBackendType = Window::kRaster_BackendType;
#endif

        SkFont font = ToolUtils::DefaultFont();
        font.setSize(18.0f);

        std::string initialText =
            "Welcome to the Skia Text Editor!\n"
            "This is a clean, immutable 4-layer text layout and editing engine.\n"
            "Features:\n"
            "- Exact 1-to-1 HarfBuzz cluster mapping (liga=0, ccmp=0)\n"
            "- UAX #9 BiDi reordering & Arabic support: مرحبا بالعالم\n"
            "- Dynamic vertical Zalgo bounding boxes: e\xcc\x81\xcc\x80\xcc\x83\xcc\x82\xcc\x88\xcc\x8a\n"
            "- Zero-heap spatial navigation & selection\n\n"
            "Try clicking, typing, backspace, and arrow keys!";

        LayoutConstraints constraints;
        constraints.max_width = 760.0f;

        fEditor = TextEditorController::Make(
            initialText,
            font,
            SkColor4f{0.1f, 0.1f, 0.12f, 1.0f},
            constraints);
    }

    ~TextEditorApp() override {
        fWindow->detach();
        delete fWindow;
    }

    void onIdle() override {}

    void onBackendCreated() override {
        fWindow->setTitle("Skia Text Editor (Project KEEPER)");
        fWindow->show();
        fWindow->inval();
    }

    void onResize(int width, int height) override {
        if (fEditor) {
            LayoutConstraints c = fEditor->constraints();
            c.max_width = std::max(100.0f, static_cast<float>(width - 40));
            fEditor->setConstraints(c);
        }
        fWindow->inval();
    }

    void onPaint(SkSurface* surface) override {
        if (!surface || !fEditor) {
            return;
        }
        auto canvas = surface->getCanvas();
        canvas->clear(SkColorSetRGB(252, 252, 254));

        PaintOptions options;
        options.origin = SkPoint::Make(20.0f, 20.0f);
        options.caret_width = 2.0f;
        options.caret_color = SkColor4f{0.15f, 0.45f, 0.95f, 1.0f};
        options.selection_color = SkColor4f{0.75f, 0.85f, 1.0f, 0.5f};
        options.show_caret = true;

        TextEditorPainter::Paint(canvas, *fEditor, options);
    }

    bool onChar(SkUnichar c, skui::ModifierKey modifiers) override {
        if (!fEditor) {
            return false;
        }
        if (fEditor->handleChar(c, modifiers)) {
            fWindow->inval();
            return true;
        }
        return false;
    }

    bool onKey(skui::Key key, skui::InputState state, skui::ModifierKey modifiers) override {
        if (!fEditor) {
            return false;
        }
        if (fEditor->handleKey(key, state, modifiers)) {
            fWindow->inval();
            return true;
        }
        return false;
    }

    bool onMouse(int x, int y, skui::InputState state, skui::ModifierKey modifiers) override {
        if (!fEditor) {
            return false;
        }
        bool shift = (modifiers & skui::ModifierKey::kShift) != skui::ModifierKey::kNone;
        SkScalar localX = x - 20.0f;
        SkScalar localY = y - 20.0f;

        if (state == skui::InputState::kDown) {
            fIsMouseDown = true;
            fEditor->moveCaretToPoint(localX, localY, shift);
            fWindow->inval();
            return true;
        } else if (state == skui::InputState::kMove && fIsMouseDown) {
            fEditor->moveCaretToPoint(localX, localY, true);
            fWindow->inval();
            return true;
        } else if (state == skui::InputState::kUp) {
            fIsMouseDown = false;
            return true;
        }
        return false;
    }

private:
    Window* fWindow;
    Window::BackendType fBackendType;
    std::unique_ptr<TextEditorController> fEditor;
    bool fIsMouseDown;
};

Application* Application::Create(int argc, char** argv, void* platformData) {
    return new TextEditorApp(argc, argv, platformData);
}
