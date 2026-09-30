#include "../../include/NavKit/module/WxApplication.h"

#include <memory>

#include <SDL.h>
#include <wx/evtloop.h>
#include <wx/sizer.h>

#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Renderer.h"

wxIMPLEMENT_APP_NO_MAIN(NavKitApp);

namespace {
    MainFrame* mainFrame = nullptr;
    std::unique_ptr<wxGLContext> renderContext;
    std::unique_ptr<wxEventLoop> guiEventLoop;
    std::unique_ptr<wxEventLoopActivator> guiEventLoopActivator;
    constexpr int renderCanvasAttributes[] = {
        WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24, WX_GL_SAMPLE_BUFFERS, 1, WX_GL_SAMPLES, 2, 0};
} // namespace

bool NavKitApp::OnInit() {
    mainFrame = new MainFrame();
    SetTopWindow(mainFrame);
    mainFrame->Show();
    mainFrame->renderPanel()->SetFocus();
    return true;
}

MainFrame::MainFrame() :
    wxFrame(nullptr, wxID_ANY, "NavKit", wxDefaultPosition, wxSize(900, 600), wxDEFAULT_FRAME_STYLE),
    panel(new wxGLCanvas(this, wxID_ANY, renderCanvasAttributes)) {
    SetMenuBar(Menu::createMenuBar());
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(panel, 1, wxEXPAND);
    SetSizer(sizer);
    Bind(wxEVT_MENU, &MainFrame::onMenu, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    Bind(wxEVT_MOVE, &MainFrame::onMove, this);
    Bind(wxEVT_SIZE, &MainFrame::onSize, this);
}

wxGLCanvas* MainFrame::renderPanel() const {
    return panel;
}

void MainFrame::onMenu(wxCommandEvent& event) {
    if (Menu::handleMenuClicked(event.GetId()) == InputHandler::QUIT) {
        SDL_Event quitEvent{};
        quitEvent.type = SDL_QUIT;
        SDL_PushEvent(&quitEvent);
    }
}

void MainFrame::onClose(wxCloseEvent& event) {
    SDL_Event quitEvent{};
    quitEvent.type = SDL_QUIT;
    SDL_PushEvent(&quitEvent);
    event.Veto();
}

void MainFrame::onMove(wxMoveEvent& event) {
    event.Skip();
    if (Renderer::getInstance().window) {
        Renderer::getInstance().handleMoved();
    }
}

void MainFrame::onSize(wxSizeEvent& event) {
    event.Skip();
    if (Renderer::getInstance().window) {
        Renderer::getInstance().handleResize();
    }
}

bool initializeWx(int& argc, char** argv) {
    if (!wxEntryStart(argc, argv)) {
        return false;
    }
    if (wxTheApp->CallOnInit()) {
        guiEventLoop = std::make_unique<wxEventLoop>();
        guiEventLoopActivator = std::make_unique<wxEventLoopActivator>(guiEventLoop.get());
        return true;
    }
    wxEntryCleanup();
    mainFrame = nullptr;
    return false;
}

bool initializeRenderContext() {
    if (!mainFrame) {
        return false;
    }

    wxGLContextAttrs contextAttributes;
    contextAttributes.PlatformDefaults().CoreProfile().OGLVersion(3, 3).EndList();
    renderContext = std::make_unique<wxGLContext>(mainFrame->renderPanel(), nullptr, &contextAttributes);
    return renderContext->IsOK() && mainFrame->renderPanel()->SetCurrent(*renderContext);
}

bool makeRenderContextCurrent() {
    return mainFrame && renderContext && renderContext->IsOK() && mainFrame->renderPanel()->SetCurrent(*renderContext);
}

void swapRenderBuffers() {
    if (mainFrame) {
        mainFrame->renderPanel()->SwapBuffers();
    }
}

void processWxEvents() {
    if (wxTheApp && guiEventLoop) {
        wxTheApp->ProcessPendingEvents();
        while (guiEventLoop->Pending()) {
            if (!guiEventLoop->Dispatch()) {
                break;
            }
            wxTheApp->ProcessPendingEvents();
        }
    }
}

void shutdownWx() {
    guiEventLoopActivator.reset();
    guiEventLoop.reset();
    renderContext.reset();
    if (wxTheApp) {
        wxTheApp->OnExit();
        wxEntryCleanup();
    }
    mainFrame = nullptr;
}

wxFrame* getMainFrame() {
    return mainFrame;
}

wxGLCanvas* getRenderPanel() {
    return mainFrame ? mainFrame->renderPanel() : nullptr;
}
