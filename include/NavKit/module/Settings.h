#pragma once
#include <SimpleIni.h>

class Settings {
    static INT_PTR SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    explicit Settings();

    CSimpleIniA ini;
    int settingsScroll;

public:
    static Settings &getInstance() {
        static Settings instance;
        return instance;
    }

    float backgroundColor;

    static void setValue(const std::string &folder, const std::string &key, const std::string &value);

    static void save();

    static void Load();

    void showSettingsDialog();

    static HWND hSettingsDialog;
};
