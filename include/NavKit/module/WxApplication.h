#pragma once

#include <wx/app.h>
#include <wx/frame.h>
#include <wx/glcanvas.h>

class NavKitApp final : public wxApp {
public:
    bool OnInit() override;
};

class MainFrame final : public wxFrame {
public:
    MainFrame();

    wxGLCanvas* renderPanel() const;

private:
    void onMenu(wxCommandEvent& event);
    void onClose(wxCloseEvent& event);
    void onMove(wxMoveEvent& event);
    void onSize(wxSizeEvent& event);
#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

    wxGLCanvas* panel;
};

bool initializeWx(int& argc, char** argv);
bool initializeRenderContext();
bool makeRenderContextCurrent();
void swapRenderBuffers();
void showMainWindow();
void processWxEvents();
void shutdownWx();
wxFrame* getMainFrame();
wxGLCanvas* getRenderPanel();
