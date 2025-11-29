#include "../../include/NavKit/module/Settings.h"

#include <CommCtrl.h>
#include <filesystem>
#include <Windows.h>
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/SceneExtract.h"

HWND Settings::hSettingsDialog = nullptr;
#pragma comment(lib, "comctl32.lib")

struct DialogSettings {
    float backgroundColor{};
    std::string hitmanFolder;
    std::string outputFolder;
    std::string blenderPath;
};

INT_PTR CALLBACK Settings::SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    auto settings = reinterpret_cast<Settings *>(GetWindowLongPtr(hDlg, GWLP_USERDATA));
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    Obj &obj = Obj::getInstance();

    switch (message) {
        case WM_INITDIALOG: {
            settings = reinterpret_cast<Settings *>(lParam);
            SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(settings));

            auto tempSettings = new DialogSettings();
            tempSettings->backgroundColor = settings->backgroundColor;
            tempSettings->hitmanFolder = sceneExtract.hitmanFolder;
            tempSettings->outputFolder = sceneExtract.outputFolder;
            tempSettings->blenderPath = obj.blenderPath;
            SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(tempSettings));

            const HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_BG_COLOR);
            SendMessage(hSlider, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG(0, 100)); // Range 0-100
            SendMessage(hSlider, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) (tempSettings->backgroundColor * 100.0f));

            SetDlgItemText(hDlg, IDC_EDIT_HITMAN_PATH, sceneExtract.hitmanFolder.c_str());
            SetDlgItemText(hDlg, IDC_EDIT_OUTPUT_PATH, sceneExtract.outputFolder.c_str());
            SetDlgItemText(hDlg, IDC_EDIT_BLENDER_PATH, obj.blenderPath.c_str());

            return TRUE;
        }

        case WM_HSCROLL: {
            if (const auto tempSettings = reinterpret_cast<DialogSettings *>(GetWindowLongPtr(hDlg, DWLP_USER))) {
                const HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_BG_COLOR);
                tempSettings->backgroundColor = static_cast<float>(SendMessage(hSlider, TBM_GETPOS, 0, 0)) / 100.0f;
            }
            return TRUE;
        }

        case WM_COMMAND: {
            const auto tempSettings = reinterpret_cast<DialogSettings *>(GetWindowLongPtr(hDlg, DWLP_USER));

            if (UINT commandId = LOWORD(wParam); commandId == IDC_BUTTON_BROWSE_HITMAN) {
                if (const char *folderName = SceneExtract::openHitmanFolderDialog(sceneExtract.hitmanFolder.data())) {
                    tempSettings->hitmanFolder = folderName;
                    SetDlgItemText(hDlg, IDC_EDIT_HITMAN_PATH, tempSettings->hitmanFolder.c_str());
                }
            } else if (commandId == IDC_BUTTON_BROWSE_OUTPUT) {
                if (const char *folderName = SceneExtract::openOutputFolderDialog(sceneExtract.outputFolder.data())) {
                    tempSettings->outputFolder = folderName;
                    SetDlgItemText(hDlg, IDC_EDIT_OUTPUT_PATH, tempSettings->outputFolder.c_str());
                }
            } else if (commandId == IDC_BUTTON_BROWSE_BLENDER) {
                if (const char *blenderFileName = Obj::openSetBlenderFileDialog(obj.blenderPath.data())) {
                    tempSettings->blenderPath = blenderFileName;
                    SetDlgItemText(hDlg, IDC_EDIT_BLENDER_PATH, tempSettings->blenderPath.c_str());
                }
            } else if (commandId == IDOK || commandId == IDC_APPLY) {
                if (tempSettings) {
                    sceneExtract.setHitmanFolder(tempSettings->hitmanFolder);
                    sceneExtract.setOutputFolder(tempSettings->outputFolder);
                    obj.setBlenderFile(tempSettings->blenderPath);

                    settings->backgroundColor = tempSettings->backgroundColor;
                    settings->setValue("Colors", "backgroundColor", std::to_string(settings->backgroundColor));
                    settings->save();
                }
                if (commandId == IDOK) {
                    DestroyWindow(hDlg);
                }
                return TRUE;
            } else if (commandId == IDCANCEL) {
                DestroyWindow(hDlg);
                return TRUE;
            }
            break;
        }

        case WM_CLOSE: {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }

        case WM_DESTROY: {
            const auto tempSettings = reinterpret_cast<DialogSettings *>(GetWindowLongPtr(hDlg, DWLP_USER));
            delete tempSettings;
            hSettingsDialog = nullptr;
            return TRUE;
        }
        default: ;
    }
    return FALSE;
}

Settings::Settings(): ini(CSimpleIniA()), settingsScroll(0), backgroundColor(0.30f) {
}

void Settings::Load() {
    CSimpleIni &ini = getInstance().ini;
    ini.SetUnicode();
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    Obj &obj = Obj::getInstance();
    Renderer &renderer = Renderer::getInstance();

    if (const SI_Error rc = ini.LoadFile("NavKit.ini"); rc < 0) {
        Logger::log(NK_ERROR, "Error loading settings from NavKit.ini");
    } else {
        Logger::log(NK_INFO, "Loading settings from NavKit.ini...");

        sceneExtract.setHitmanFolder(ini.GetValue("Paths", "hitman", "default"));
        sceneExtract.setOutputFolder(ini.GetValue("Paths", "output", "default"));
        obj.setBlenderFile(ini.GetValue("Paths", "blender", "default"));
        Grid &grid = Grid::getInstance();
        grid.saveSpacing(static_cast<float>(atof(ini.GetValue("Airg", "spacing", "2.0f"))));
        renderer.initFrameRate(static_cast<float>(atof(ini.GetValue("Renderer", "frameRate", "-1.0f"))));
        getInstance().backgroundColor = static_cast<float>(atof(ini.GetValue("Colors", "backgroundColor", "0.16f")));
    }
}

void Settings::showSettingsDialog() {
    if (hSettingsDialog) {
        SetForegroundWindow(hSettingsDialog);
        return;
    }
    HINSTANCE hInstance = GetModuleHandle(NULL);
    HWND hParentWnd = Renderer::getInstance().hwnd;
    hSettingsDialog = CreateDialogParam(
        hInstance,
        MAKEINTRESOURCE(IDD_NAVKIT_SETTINGS),
        hParentWnd,
        SettingsDialogProc,
        (LPARAM) this
    );

    if (hSettingsDialog) {
        RECT parentRect, dialogRect;
        GetWindowRect(hParentWnd, &parentRect);
        GetWindowRect(hSettingsDialog, &dialogRect);

        int parentWidth = parentRect.right - parentRect.left;
        int parentHeight = parentRect.bottom - parentRect.top;
        int dialogWidth = dialogRect.right - dialogRect.left;
        int dialogHeight = dialogRect.bottom - dialogRect.top;

        int newX = parentRect.left + (parentWidth - dialogWidth) / 2;
        int newY = parentRect.top + (parentHeight - dialogHeight) / 2;

        SetWindowPos(hSettingsDialog, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ShowWindow(hSettingsDialog, SW_SHOW);
    }
}

void Settings::save() {
    if (getInstance().ini.SaveFile("NavKit.ini") == SI_FAIL) {
        Logger::log(NK_ERROR, "Error saving settings file");
    }
}

void Settings::setValue(const std::string &folder, const std::string &key, const std::string &value) {
    if (getInstance().ini.SetValue(folder.c_str(), key.c_str(), value.c_str()) == SI_FAIL) {
        Logger::log(NK_ERROR, "Error updating setting");
    }
}
