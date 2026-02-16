#pragma once
#include <SDL_syswm.h>
#include <string>

class ErrorHandler {
public:
    static INT_PTR CALLBACK ErrorDialogHandler(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    static void openErrorDialog(const std::string& message);

    static std::string* errorMessage;
};
