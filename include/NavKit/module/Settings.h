#pragma once
#include <SimpleIni.h>

class Settings {
    static INT_PTR SettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    explicit Settings();

    CSimpleIniA ini;
public:
    static Settings &getInstance() {
        static Settings instance;
        return instance;
    }

    float backgroundColor;
    bool hitmanSet;
    bool outputSet;
    bool blenderSet;
    std::string hitmanFolder;
    std::string outputFolder;
    std::string blenderPath;

    void setValue(const std::string &folder, const std::string &key, const std::string &value);

    void setHitmanFolder(const std::string &folderName);

    void setOutputFolder(const std::string &folderName);

    void setBlenderFile(const std::string &fileName);

    void save() const;

    void load();

    void showSettingsDialog();

    static HWND hSettingsDialog;
};
