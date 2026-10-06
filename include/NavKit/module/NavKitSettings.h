#pragma once
#include <SimpleIni.h>
#include <wx/dialog.h>

struct DialogSettings {
    float backgroundColor{};
    std::string hitmanFolder;
    std::string outputFolder;
    std::string blenderPath;
    bool showDebugLogs;
    unsigned int maxThreads;
};

class NavKitSettings {
    static void resetDefaults(DialogSettings& settings);

    static void setDialogInputs(wxDialog* dialog, const DialogSettings& tempSettings);

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
    bool showDebugLogs;
    bool shouldOpenSettingsDialog;
    static wxDialog* hSettingsDialog;

    void setHitmanFolder(const std::string& folderName);

    void setOutputFolder(const std::string& folderName);

    void setBlenderFile(const std::string& fileName);

    void showNavKitSettingsDialog();

    void loadSettings();
};
