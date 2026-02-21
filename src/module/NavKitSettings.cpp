#include "../../include/NavKit/module/NavKitSettings.h"

#include <CommCtrl.h>
#include <filesystem>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/SceneExtract.h"

HWND NavKitSettings::hSettingsDialog = nullptr;
#pragma comment(lib, "comctl32.lib")

void NavKitSettings::resetDefaults(DialogSettings& settings) {
    settings.backgroundColor = 0.16f;
    settings.hitmanFolder = R"(C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3)";
    settings.outputFolder = R"(D:\workspace\output)";
    settings.blenderPath = R"(C:\Program Files\Blender Foundation\Blender 3.4\blender.exe)";
    settings.showDebugLogs = false;
}

void NavKitSettings::setDialogInputs(const HWND hDlg, const DialogSettings& tempSettings) {
    const HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_BG_COLOR);
    SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessage(hSlider, TBM_SETPOS, TRUE, tempSettings.backgroundColor * 100.0f);
    SetDlgItemText(hDlg, IDC_EDIT_HITMAN_PATH, tempSettings.hitmanFolder.c_str());
    SetDlgItemText(hDlg, IDC_EDIT_OUTPUT_PATH, tempSettings.outputFolder.c_str());
    SetDlgItemText(hDlg, IDC_EDIT_BLENDER_PATH, tempSettings.blenderPath.c_str());
    CheckDlgButton(hDlg, IDC_CHECK_SHOW_DEBUG_LOGS, tempSettings.showDebugLogs ? BST_CHECKED : BST_UNCHECKED);
}

INT_PTR CALLBACK NavKitSettings::SettingsDialogProc(const HWND hDlg, const UINT message, const WPARAM wParam,
                                                    const LPARAM lParam) {
    auto navKitSettings = reinterpret_cast<NavKitSettings*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));

    switch (message) {
    case WM_INITDIALOG: {
        navKitSettings = reinterpret_cast<NavKitSettings*>(lParam);
        SetWindowLongPtr(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(navKitSettings));

        auto tempSettings = new DialogSettings();
        tempSettings->backgroundColor = navKitSettings->backgroundColor;
        tempSettings->hitmanFolder = navKitSettings->hitmanFolder;
        tempSettings->outputFolder = navKitSettings->outputFolder;
        tempSettings->blenderPath = navKitSettings->blenderPath;
        tempSettings->showDebugLogs = navKitSettings->showDebugLogs;
        SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(tempSettings));

        setDialogInputs(hDlg, *tempSettings);

        return TRUE;
    }

    case WM_HSCROLL: {
        if (const auto tempSettings = reinterpret_cast<DialogSettings*>(GetWindowLongPtr(hDlg, DWLP_USER))) {
            const HWND hSlider = GetDlgItem(hDlg, IDC_SLIDER_BG_COLOR);
            tempSettings->backgroundColor = static_cast<float>(SendMessage(hSlider, TBM_GETPOS, 0, 0)) / 100.0f;
        }
        return TRUE;
    }

    case WM_COMMAND: {
        const auto tempSettings = reinterpret_cast<DialogSettings*>(GetWindowLongPtr(hDlg, DWLP_USER));

        if (const UINT commandId = LOWORD(wParam); commandId == IDC_BUTTON_BROWSE_HITMAN) {
            if (const char* folderName =
                SceneExtract::openHitmanFolderDialog(navKitSettings->hitmanFolder.data())) {
                tempSettings->hitmanFolder = folderName;
                SetDlgItemText(hDlg, IDC_EDIT_HITMAN_PATH, tempSettings->hitmanFolder.c_str());
            }
        } else if (commandId == IDC_BUTTON_BROWSE_OUTPUT) {
            if (const char* folderName =
                SceneExtract::openOutputFolderDialog(navKitSettings->outputFolder.data())) {
                tempSettings->outputFolder = folderName;
                SetDlgItemText(hDlg, IDC_EDIT_OUTPUT_PATH, tempSettings->outputFolder.c_str());
            }
        } else if (commandId == IDC_BUTTON_BROWSE_BLENDER) {
            if (const char* blenderFileName = SceneMesh::openSetBlenderFileDialog()) {
                tempSettings->blenderPath = blenderFileName;
                SetDlgItemText(hDlg, IDC_EDIT_BLENDER_PATH, tempSettings->blenderPath.c_str());
            }
        } else if (commandId == IDC_CHECK_SHOW_DEBUG_LOGS) {
            tempSettings->showDebugLogs = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_DEBUG_LOGS);
            Logger::log(NK_INFO, "Show Debug logs set to %s.", tempSettings->showDebugLogs ? "true" : "false");
            CheckDlgButton(hDlg, IDC_CHECK_SHOW_DEBUG_LOGS,
                           tempSettings->showDebugLogs ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        } else if (commandId == IDC_BUTTON_RESET_DEFAULTS) {
            navKitSettings->resetDefaults(*tempSettings);
            setDialogInputs(hDlg, *tempSettings);
        } else if (commandId == IDOK || commandId == IDC_APPLY) {
            if (tempSettings) {
                navKitSettings->backgroundColor = tempSettings->backgroundColor;
                navKitSettings->setHitmanFolder(tempSettings->hitmanFolder);
                navKitSettings->setOutputFolder(tempSettings->outputFolder);
                navKitSettings->setBlenderFile(tempSettings->blenderPath);
                navKitSettings->showDebugLogs = tempSettings->showDebugLogs;

                PersistedSettings& persistedSettings = PersistedSettings::getInstance();
                persistedSettings.setValue("NavKit", "backgroundColor",
                                           std::to_string(navKitSettings->backgroundColor));
                persistedSettings.setValue("NavKit", "hitman", tempSettings->hitmanFolder);
                persistedSettings.setValue("NavKit", "output", tempSettings->outputFolder);
                persistedSettings.setValue("NavKit", "blender", tempSettings->blenderPath);
                persistedSettings.setValue("NavKit", "showDebugLogs", tempSettings->showDebugLogs ? "true" : "false");
                persistedSettings.save();
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
        const auto tempSettings = reinterpret_cast<DialogSettings*>(GetWindowLongPtr(hDlg, DWLP_USER));
        delete tempSettings;
        hSettingsDialog = nullptr;
        return TRUE;
    }
    default: ;
    }
    return FALSE;
}

NavKitSettings::NavKitSettings() : backgroundColor(0.30f),
                                   hitmanSet(false),
                                   outputSet(false),
                                   blenderSet(false),
                                   blenderPath(R"("C:\Program Files\Blender Foundation\Blender 3.4\blender.exe")"),
                                   showDebugLogs(false) {}

void NavKitSettings::showNavKitSettingsDialog() {
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

        const int parentWidth = parentRect.right - parentRect.left;
        const int parentHeight = parentRect.bottom - parentRect.top;
        const int dialogWidth = dialogRect.right - dialogRect.left;
        const int dialogHeight = dialogRect.bottom - dialogRect.top;

        const int newX = parentRect.left + (parentWidth - dialogWidth) / 2;
        const int newY = parentRect.top + (parentHeight - dialogHeight) / 2;

        SetWindowPos(hSettingsDialog, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ShowWindow(hSettingsDialog, SW_SHOW);
    }
}

void NavKitSettings::loadSettings() {
    const PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    backgroundColor = static_cast<float>(atof(persistedSettings.getValue("NavKit", "backgroundColor", "0.16f")));
    setHitmanFolder(persistedSettings.getValue("NavKit", "hitman", "default"));
    setOutputFolder(persistedSettings.getValue("NavKit", "output", "default"));
    setBlenderFile(persistedSettings.getValue("NavKit", "blender", "default"));
    showDebugLogs = strcmp(persistedSettings.getValue("NavKit", "showDebugLogs", "false"), "true") == 0;
}

void NavKitSettings::setHitmanFolder(const std::string& folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        hitmanSet = true;
        bool shouldReInitExtractionData = false;
        if (folderName != hitmanFolder) {
            shouldReInitExtractionData = true;
        }
        hitmanFolder = folderName;
        Logger::log(NK_INFO, ("Setting Hitman folder to: " + hitmanFolder).c_str());
        if (shouldReInitExtractionData) {
            Rpkg::backgroundWorker.emplace(&Rpkg::initExtractionData);
        }
    } else {
        Logger::log(NK_WARN, ("Could not find Hitman folder: " + hitmanFolder).c_str());
    }
}

void NavKitSettings::setOutputFolder(const std::string& folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        outputSet = true;
        outputFolder = folderName;
        Logger::log(NK_INFO, ("Setting output folder to: " + outputFolder).c_str());
    } else {
        Logger::log(NK_WARN, ("Could not find output folder: " + outputFolder).c_str());
    }
}

void NavKitSettings::setBlenderFile(const std::string& fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        blenderSet = true;
        blenderPath = fileName;
        Logger::log(NK_INFO, ("Setting Blender exe path to: " + blenderPath).c_str());
    } else {
        Logger::log(NK_WARN, ("Could not find Blender exe path: " + blenderPath).c_str());
    }
}
