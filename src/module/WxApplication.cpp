#include "../../include/NavKit/module/WxApplication.h"

#include <memory>

#include <SDL.h>
#include <wx/evtloop.h>
#include <wx/filename.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/stdpaths.h>
#include <wx/sizer.h>
#ifdef __WXMSW__
#include <windows.h>
#endif

#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/util/FileUtil.h"

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
#ifdef __WXMSW__
    // Native menus inherit the theme selected before creating any windows.
    PersistedSettings& settings = PersistedSettings::getInstance();
    settings.loadFile();
    if (std::string(settings.getValue("NavKit", "darkMode", "true")) == "true") {
        MSWEnableDarkMode(DarkMode_Always);
    }
#endif
    mainFrame = new MainFrame();
    SetTopWindow(mainFrame);
    return true;
}

MainFrame::MainFrame() :
    wxFrame(nullptr, wxID_ANY, "NavKit", wxDefaultPosition, wxSize(900, 600), wxDEFAULT_FRAME_STYLE),
    // wxWANTS_CHARS: the 3D view uses Tab, Enter and the arrow keys itself, so they must not be taken for navigation.
    panel(new wxGLCanvas(this, wxID_ANY, renderCanvasAttributes, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS)) {
    SetMenuBar(Menu::createMenuBar());
    wxInitAllImageHandlers();
    const std::string iconFilePath = FileUtil::getApplicationResourcePath("NavKitLogo.png").string();
    wxFileName iconPath(wxString::FromUTF8(iconFilePath));
    const wxIconBundle icons(iconPath.GetFullPath(), wxBITMAP_TYPE_PNG);
    if (icons.GetIconCount() == 0) {
        Logger::log(NK_ERROR, "Could not load the NavKit application icon from %s.",
            iconPath.GetFullPath().ToStdString().c_str());
    } else {
        SetIcons(icons);
    }
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(panel, 1, wxEXPAND);
    SetSizer(sizer);
    Bind(wxEVT_MENU, &MainFrame::onMenu, this);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    Bind(wxEVT_SIZE, &MainFrame::onSize, this);
    // Mouse and keyboard input for the 3D view. wxWidgets owns the canvas's native view, so it is what receives the
    // events; SDL does not get them (on macOS the wxWidgets view consumes them before SDL's responder sees them).
    panel->Bind(wxEVT_MOTION, &MainFrame::onCanvasMouseMove, this);
    panel->Bind(wxEVT_LEFT_DOWN, &MainFrame::onCanvasMouseDown, this);
    panel->Bind(wxEVT_RIGHT_DOWN, &MainFrame::onCanvasMouseDown, this);
    panel->Bind(wxEVT_LEFT_UP, &MainFrame::onCanvasMouseUp, this);
    panel->Bind(wxEVT_RIGHT_UP, &MainFrame::onCanvasMouseUp, this);
    panel->Bind(wxEVT_MOUSE_CAPTURE_LOST, &MainFrame::onCanvasMouseCaptureLost, this);
    panel->Bind(wxEVT_MOUSEWHEEL, &MainFrame::onCanvasMouseWheel, this);
    panel->Bind(wxEVT_KEY_DOWN, &MainFrame::onCanvasKeyDown, this);
    panel->Bind(wxEVT_KEY_UP, &MainFrame::onCanvasKeyUp, this);
    panel->Bind(wxEVT_KILL_FOCUS, &MainFrame::onCanvasKillFocus, this);
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
    Renderer::getInstance().persistWindowSettings();
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

void MainFrame::onCanvasMouseMove(wxMouseEvent& event) {
    const wxPoint position = event.GetPosition();
    InputHandler::getInstance().handleMouseMove(position.x, position.y);
    event.Skip();
}

void MainFrame::onCanvasMouseDown(wxMouseEvent& event) {
    // Clicking the 3D view gives it the keyboard, so Tab and Alt+Enter keep working after using the menu bar.
    if (!panel->HasFocus()) {
        panel->SetFocus();
    }
    InputHandler& inputHandler = InputHandler::getInstance();
    const wxPoint position = event.GetPosition();
    inputHandler.handleMouseMove(position.x, position.y);
    if (event.RightDown()) {
        inputHandler.handleMouseButtonDown(InputHandler::RIGHT_MOUSE_BUTTON);
        // Keep receiving motion events while rotating, even when the pointer leaves the canvas.
        if (!panel->HasCapture()) {
            panel->CaptureMouse();
        }
    } else if (event.LeftDown()) {
        inputHandler.handleMouseButtonDown(InputHandler::LEFT_MOUSE_BUTTON);
    }
}

void MainFrame::onCanvasMouseUp(wxMouseEvent& event) {
    InputHandler& inputHandler = InputHandler::getInstance();
    const wxPoint position = event.GetPosition();
    inputHandler.handleMouseMove(position.x, position.y);
    if (event.RightUp()) {
        if (panel->HasCapture()) {
            panel->ReleaseMouse();
        }
        inputHandler.handleMouseButtonUp(InputHandler::RIGHT_MOUSE_BUTTON);
    } else if (event.LeftUp()) {
        inputHandler.handleMouseButtonUp(InputHandler::LEFT_MOUSE_BUTTON);
    }
}

void MainFrame::onCanvasMouseCaptureLost(wxMouseCaptureLostEvent&) {
    InputHandler::getInstance().handleMouseButtonUp(InputHandler::RIGHT_MOUSE_BUTTON);
}

void MainFrame::onCanvasMouseWheel(wxMouseEvent& event) {
    if (event.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL) {
        InputHandler::getInstance().handleMouseWheel(event.GetWheelRotation(), event.GetWheelDelta());
    }
}

void MainFrame::onCanvasKeyDown(wxKeyEvent& event) {
    if (!InputHandler::getInstance().handleKeyDown(event.GetKeyCode(), event.AltDown())) {
        event.Skip();
    }
}

void MainFrame::onCanvasKeyUp(wxKeyEvent& event) {
    if (!InputHandler::getInstance().handleKeyUp(event.GetKeyCode())) {
        event.Skip();
    }
}

void MainFrame::onCanvasKillFocus(wxFocusEvent& event) {
    // The key-up events for keys held while the keyboard goes elsewhere would never arrive.
    InputHandler::getInstance().handleKeyboardFocusLost();
    event.Skip();
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

void showMainWindow() {
    if (mainFrame) {
        mainFrame->Show();
        mainFrame->renderPanel()->SetFocus();
    }
}

void processWxEvents() {
    if (!wxTheApp || !guiEventLoop) {
        return;
    }
    wxTheApp->ProcessPendingEvents();
    // Dispatch everything that is already queued without ever blocking: DispatchTimeout(0) returns 1 after
    // dispatching one native event, -1 when none is waiting and 0 when the loop was asked to exit. Pending() and
    // Dispatch() cannot be used for this, because on macOS Pending() always returns true and Dispatch() waits up to a
    // second for an event, so a Pending()/Dispatch() loop never returns to the frame loop there.
    while (guiEventLoop->DispatchTimeout(0) == 1) {
        wxTheApp->ProcessPendingEvents();
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
