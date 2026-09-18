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
#include "tools/text_editor/include/TextEditorPainter.h"
#include "tools/text_editor/include/TextEditorViewModel.h"
#include "tools/window/DisplayParams.h"

using namespace sk_app;
using namespace skia::text_editor;

class TextEditorApp : public Application, public Window::Layer {
public:
    TextEditorApp(int argc, char** argv, void* platformData)
        : fBackendType(Window::kRaster_BackendType)
        , fIsMouseDown(false)
        , fPadding(24.0f)
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

        fViewModel = std::make_unique<TextEditorViewModel>(
            initialText,
            font,
            SkColor4f{0.1f, 0.1f, 0.12f, 1.0f},
            constraints);

        fViewModel->setOnRedrawCallback([this]() {
            fWindow->inval();
        });
    }

    ~TextEditorApp() override {
        fWindow->detach();
        delete fWindow;
    }

    void onIdle() override {}

    void onBackendCreated() override {
        fWindow->setTitle("Skia Text Editor (Project KEEPER - MVVM)");
        fWindow->show();
        fWindow->inval();
    }

    void onResize(int width, int height) override {
        if (fViewModel) {
            LayoutConstraints c = fViewModel->document().constraints();
            c.max_width = std::max(100.0f, static_cast<float>(width - 2 * fPadding));
            fViewModel->document().setConstraints(c);
        }
        fWindow->inval();
    }

    void onPaint(SkSurface* surface) override {
        if (!surface || !fViewModel) {
            return;
        }
        auto canvas = surface->getCanvas();
        canvas->clear(SkColorSetRGB(252, 252, 254));

        canvas->save();
        canvas->translate(fPadding, fPadding);

        PaintOptions options;
        options.origin = SkPoint::Make(0, 0); // Text editor starts at (0, 0)
        options.caret_width = 2.0f;
        options.caret_color = SkColor4f{0.15f, 0.45f, 0.95f, 1.0f};
        options.selection_color = SkColor4f{0.75f, 0.85f, 1.0f, 0.5f};
        options.show_caret = true;

        TextEditorPainter::Paint(canvas, *fViewModel, options);
        canvas->restore();
    }

    bool onChar(SkUnichar c, skui::ModifierKey modifiers) override {
        if (!fViewModel) {
            return false;
        }
        return fViewModel->handleChar(c, modifiers);
    }

    bool onKey(skui::Key key, skui::InputState state, skui::ModifierKey modifiers) override {
        if (!fViewModel) {
            return false;
        }
        return fViewModel->handleKey(key, state, modifiers);
    }

    bool onMouse(int x, int y, skui::InputState state, skui::ModifierKey modifiers) override {
        if (!fViewModel) {
            return false;
        }
        bool shift = (modifiers & skui::ModifierKey::kShift) != skui::ModifierKey::kNone;
        SkScalar localX = static_cast<SkScalar>(x) - fPadding;
        SkScalar localY = static_cast<SkScalar>(y) - fPadding;

        if (state == skui::InputState::kDown) {
            fIsMouseDown = true;
            fViewModel->moveCaretToPoint(localX, localY, shift);
            return true;
        } else if (state == skui::InputState::kMove && fIsMouseDown) {
            fViewModel->moveCaretToPoint(localX, localY, true);
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
    std::unique_ptr<TextEditorViewModel> fViewModel;
    bool fIsMouseDown;
    SkScalar fPadding;
};

Application* Application::Create(int argc, char** argv, void* platformData) {
    return new TextEditorApp(argc, argv, platformData);
}
