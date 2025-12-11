#include "../../include/NavKit/module/Settings.h"

#include <CommCtrl.h>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
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

    switch (message) {
        case WM_INITDIALOG: {
            settings = reinterpret_cast<Settings *>(lParam);
            SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(settings));

            auto tempSettings = new DialogSettings();
            tempSettings->backgroundColor = settings->backgroundColor;
            tempSettings->hitmanFolder = settings->hitmanFolder;
            tempSettings->outputFolder = settings->outputFolder;
            tempSettings->blenderPath = settings->blenderPath;
            SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(tempSettings));

            const HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_BG_COLOR);
            SendMessage(hSlider, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG(0, 100)); // Range 0-100
            SendMessage(hSlider, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) (tempSettings->backgroundColor * 100.0f));

            SetDlgItemText(hDlg, IDC_EDIT_HITMAN_PATH, settings->hitmanFolder.c_str());
            SetDlgItemText(hDlg, IDC_EDIT_OUTPUT_PATH, settings->outputFolder.c_str());
            SetDlgItemText(hDlg, IDC_EDIT_BLENDER_PATH, settings->blenderPath.c_str());

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
                if (const char *folderName = SceneExtract::openHitmanFolderDialog(settings->hitmanFolder.data())) {
                    tempSettings->hitmanFolder = folderName;
                    SetDlgItemText(hDlg, IDC_EDIT_HITMAN_PATH, tempSettings->hitmanFolder.c_str());
                }
            } else if (commandId == IDC_BUTTON_BROWSE_OUTPUT) {
                if (const char *folderName = SceneExtract::openOutputFolderDialog(settings->outputFolder.data())) {
                    tempSettings->outputFolder = folderName;
                    SetDlgItemText(hDlg, IDC_EDIT_OUTPUT_PATH, tempSettings->outputFolder.c_str());
                }
            } else if (commandId == IDC_BUTTON_BROWSE_BLENDER) {
                if (const char *blenderFileName = Obj::openSetBlenderFileDialog(settings->blenderPath.data())) {
                    tempSettings->blenderPath = blenderFileName;
                    SetDlgItemText(hDlg, IDC_EDIT_BLENDER_PATH, tempSettings->blenderPath.c_str());
                }
            } else if (commandId == IDOK || commandId == IDC_APPLY) {
                if (tempSettings) {
                    settings->setHitmanFolder(tempSettings->hitmanFolder);
                    settings->setOutputFolder(tempSettings->outputFolder);
                    settings->setBlenderFile(tempSettings->blenderPath);

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
            DestroyWindow(hDlg);
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

Settings::Settings() : ini(CSimpleIniA()),
                       backgroundColor(0.30f),
                       hitmanSet(false),
                       outputSet(false),
                       blenderSet(false),
                       blenderPath(R"("C:\Program Files\Blender Foundation\Blender 3.4\blender.exe")") {
}

void Settings::load() {
    CSimpleIni &ini = getInstance().ini;
    ini.SetUnicode();
    Renderer &renderer = Renderer::getInstance();

    if (const SI_Error rc = ini.LoadFile("NavKit.ini"); rc < 0) {
        Logger::log(NK_ERROR, "Error loading settings from NavKit.ini");
    } else {
        Logger::log(NK_INFO, "Loading settings from NavKit.ini...");

        setHitmanFolder(ini.GetValue("Paths", "hitman", "default"));
        setOutputFolder(ini.GetValue("Paths", "output", "default"));
        setBlenderFile(ini.GetValue("Paths", "blender", "default"));
        Grid &grid = Grid::getInstance();
        grid.saveSpacing(static_cast<float>(atof(ini.GetValue("Airg", "spacing", "2.0f"))));
        renderer.initFrameRate(static_cast<float>(atof(ini.GetValue("Renderer", "frameRate", "-1.0f"))));
        getInstance().backgroundColor = static_cast<float>(atof(ini.GetValue("Colors", "backgroundColor", "0.16f")));

        Obj &obj = Obj::getInstance();
        const char *meshTypeStr = ini.GetValue("Obj", "MeshTypeForBuild", "ALOC");
        if (strcmp(meshTypeStr, "PRIM") == 0) {
            obj.meshTypeForBuild = PRIM;
        } else {
            obj.meshTypeForBuild = ALOC;
        }

        const char *primLodsStr = ini.GetValue("Obj", "PrimLods", "11111111");
        for (int i = 0; i < 8; ++i) {
            if (i < strlen(primLodsStr)) {
                obj.primLods[i] = primLodsStr[i] == '1';
            } else {
                obj.primLods[i] = true;
            }
        }
    }
}

void Settings::showSettingsDialog() {
    if (hSettingsDialog) {
        SetForegroundWindow(hSettingsDialog);
        return;
    }
    const HINSTANCE hInstance = GetModuleHandle(nullptr);
    const HWND hParentWnd = Renderer::hwnd;
    hSettingsDialog = CreateDialogParam(
        hInstance,
        MAKEINTRESOURCE(IDD_NAVKIT_SETTINGS),
        hParentWnd,
        SettingsDialogProc,
        reinterpret_cast<LPARAM>(this)
    );

    if (hSettingsDialog) {
        if (HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON))) {
            SendMessage(hSettingsDialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessage(hSettingsDialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }
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

void Settings::save() const {
    if (ini.SaveFile("NavKit.ini") == SI_FAIL) {
        Logger::log(NK_ERROR, "Error saving settings file");
    }
}

void Settings::setValue(const std::string &folder, const std::string &key, const std::string &value) {
    if (ini.SetValue(folder.c_str(), key.c_str(), value.c_str()) == SI_FAIL) {
        Logger::log(NK_ERROR, "Error updating setting");
    }
}

void Settings::setHitmanFolder(const std::string &folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        hitmanSet = true;
        hitmanFolder = folderName;
        Logger::log(NK_INFO, ("Setting Hitman folder to: " + hitmanFolder).c_str());
        setValue("Paths", "hitman", folderName);
        save();
    } else {
        Logger::log(NK_WARN, ("Could not find Hitman folder: " + hitmanFolder).c_str());
    }
}

void Settings::setOutputFolder(const std::string &folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        outputSet = true;
        outputFolder = folderName;
        Logger::log(NK_INFO, ("Setting output folder to: " + outputFolder).c_str());
        setValue("Paths", "output", folderName);
        save();
    } else {
        Logger::log(NK_WARN, ("Could not find output folder: " + outputFolder).c_str());
    }
}

void Settings::setBlenderFile(const std::string &fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        blenderSet = true;
        blenderPath = fileName;
        Logger::log(NK_INFO, ("Setting Blender exe path to: " + blenderPath).c_str());
        setValue("Paths", "blender", fileName);
        save();
    } else {
        Logger::log(NK_WARN, ("Could not find Blender exe path: " + blenderPath).c_str());
    }
}
