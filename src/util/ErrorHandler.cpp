#include "../../include/NavKit/util/ErrorHandler.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/Resource.h"

std::string *ErrorHandler::errorMessage = nullptr;

INT_PTR CALLBACK ErrorHandler::ErrorDialogHandler(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            HWND hStatic = GetDlgItem(hwndDlg, IDC_ERROR_TEXT);
            if (errorMessage) {
                SetWindowTextA(hStatic, errorMessage->c_str());
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                EndDialog(hwndDlg, IDOK);
                return TRUE;
            }
        if (LOWORD(wParam) == IDC_COPY_BUTTON) {
            OpenClipboard(hwndDlg);
            EmptyClipboard();
            if (errorMessage) {
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, errorMessage->length() + 1);
                char *data = (char *) GlobalLock(hMem);
                strcpy_s(data, errorMessage->length() + 1, errorMessage->c_str());
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
                CloseClipboard();
                GlobalFree(hMem);
            }
            return TRUE;
        }
        break;
        default: return FALSE;
    }
    return FALSE;
}

void ErrorHandler::openErrorDialog(const std::string& message) {
    errorMessage = new std::string(message);
    DialogBoxParamA(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_ERROR_DIALOG), Renderer::hwnd, ErrorDialogHandler, 0);
}
