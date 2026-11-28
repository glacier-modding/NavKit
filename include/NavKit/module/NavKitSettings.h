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
    bool darkMode;
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
    bool darkMode;
    bool shouldOpenSettingsDialog;
    static wxDialog* hSettingsDialog;

    void setHitmanFolder(const std::string& folderName);

    void setOutputFolder(const std::string& folderName);

    void setBlenderFile(const std::string& fileName);

    /// Opens the dialog over @p parent, or over NavKit's main window when none is given.
    void showNavKitSettingsDialog(wxWindow* parent = nullptr);

    void loadSettings();
};
