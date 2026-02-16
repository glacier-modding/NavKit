#pragma once
#include <SimpleIni.h>

struct DialogSettings {
    float backgroundColor{};
    std::string hitmanFolder;
    std::string outputFolder;
    std::string blenderPath;
};

class NavKitSettings {
    static void resetDefaults(DialogSettings& settings);

    static void setDialogInputs(HWND hDlg, const DialogSettings& tempSettings);

    static INT_PTR SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    explicit NavKitSettings();

public:
    static NavKitSettings& getInstance() {
        static NavKitSettings instance;
        return instance;
    }

    float backgroundColor;
    bool hitmanSet;
    bool outputSet;
    bool blenderSet;
    std::string hitmanFolder;
    std::string outputFolder;
    std::string blenderPath;
    static HWND hSettingsDialog;

    void setHitmanFolder(const std::string& folderName);

    void setOutputFolder(const std::string& folderName);

    void setBlenderFile(const std::string& fileName);

    void showNavKitSettingsDialog();

    void loadSettings();
};
