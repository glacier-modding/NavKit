#include "../../include/NavKit/module/NavKitSettings.h"

#include <filesystem>
#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/slider.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <array>
#include <memory>
#include <tuple>

wxDialog* NavKitSettings::hSettingsDialog = nullptr;

void NavKitSettings::resetDefaults(DialogSettings& settings) {
    settings.backgroundColor = 0.16f;
    settings.hitmanFolder = R"(C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3)";
    settings.outputFolder = R"(D:\workspace\output)";
    settings.blenderPath = R"(C:\Program Files\Blender Foundation\Blender 4.3\blender.exe)";
    settings.showDebugLogs = false;
}

void NavKitSettings::setDialogInputs(wxDialog* dialog, const DialogSettings& tempSettings) {
    static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_BG_COLOR))
        ->SetValue(static_cast<int>(tempSettings.backgroundColor * 100.0f));
    static_cast<wxTextCtrl*>(dialog->FindWindow(IDC_EDIT_HITMAN_PATH))
        ->ChangeValue(wxString::FromUTF8(tempSettings.hitmanFolder));
    static_cast<wxTextCtrl*>(dialog->FindWindow(IDC_EDIT_OUTPUT_PATH))
        ->ChangeValue(wxString::FromUTF8(tempSettings.outputFolder));
    static_cast<wxTextCtrl*>(dialog->FindWindow(IDC_EDIT_BLENDER_PATH))
        ->ChangeValue(wxString::FromUTF8(tempSettings.blenderPath));
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_SHOW_DEBUG_LOGS))->SetValue(tempSettings.showDebugLogs);
}

NavKitSettings::NavKitSettings() :
    backgroundColor(0.30f), hitmanSet(false), outputSet(false), blenderSet(false), showDebugLogs(false),
    shouldOpenSettingsDialog(false) {}

void NavKitSettings::showNavKitSettingsDialog() {
    if (hSettingsDialog) {
        hSettingsDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "NavKit Settings", wxDefaultPosition, wxSize(640, 330));
    hSettingsDialog = dialog;
    auto settings = std::make_shared<DialogSettings>(
        DialogSettings{backgroundColor, hitmanFolder, outputFolder, blenderPath, showDebugLogs});
    auto* layout = new wxFlexGridSizer(5, 3, 10, 8);
    layout->Add(new wxStaticText(dialog, wxID_ANY, "Background Color:"), 0, wxALIGN_CENTER_VERTICAL);
    layout->Add(new wxSlider(dialog, IDC_SLIDER_BG_COLOR, 0, 0, 100), 1, wxEXPAND);
    layout->AddSpacer(1);
    const std::array<std::tuple<int, int, const char*>, 3> paths = {{
        {IDC_EDIT_HITMAN_PATH, IDC_BUTTON_BROWSE_HITMAN, "Hitman Directory:"},
        {IDC_EDIT_OUTPUT_PATH, IDC_BUTTON_BROWSE_OUTPUT, "Output Directory:"},
        {IDC_EDIT_BLENDER_PATH, IDC_BUTTON_BROWSE_BLENDER, "Blender Executable:"},
    }};
    for (const auto& [textId, buttonId, label] : paths) {
        layout->Add(new wxStaticText(dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        layout->Add(new wxTextCtrl(dialog, textId, {}, wxDefaultPosition, wxDefaultSize, wxTE_READONLY), 1, wxEXPAND);
        layout->Add(new wxButton(dialog, buttonId, "Browse..."));
    }
    layout->Add(new wxStaticText(dialog, wxID_ANY, "Show Debug logs:"), 0, wxALIGN_CENTER_VERTICAL);
    layout->Add(new wxCheckBox(dialog, IDC_CHECK_SHOW_DEBUG_LOGS, "Show Debug logs"), 0, wxALIGN_CENTER_VERTICAL);
    layout->AddSpacer(1);
    layout->AddGrowableCol(1, 1);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* reset = new wxButton(dialog, IDC_BUTTON_RESET_DEFAULTS, "Reset Defaults");
    auto* ok = new wxButton(dialog, wxID_OK, "OK");
    auto* cancel = new wxButton(dialog, wxID_CANCEL, "Cancel");
    auto* apply = new wxButton(dialog, IDC_APPLY, "Apply");
    buttons->Add(reset, 0, wxRIGHT, 10);
    buttons->Add(ok, 0, wxRIGHT, 6);
    buttons->Add(cancel, 0, wxRIGHT, 6);
    buttons->Add(apply);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(layout, 1, wxEXPAND | wxALL, 12);
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    dialog->SetSizer(sizer);
    setDialogInputs(dialog, *settings);
    static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_BG_COLOR))
        ->Bind(wxEVT_SLIDER, [settings](wxCommandEvent& event) {
            settings->backgroundColor = static_cast<float>(event.GetInt()) / 100.0f;
        });

    dialog->Bind(wxEVT_BUTTON, [this, dialog, settings](wxCommandEvent& event) {
        if (event.GetId() == IDC_BUTTON_BROWSE_HITMAN) {
            if (const char* folder = SceneExtract::openHitmanFolderDialog(hitmanFolder.data())) {
                settings->hitmanFolder = folder;
            }
        } else if (event.GetId() == IDC_BUTTON_BROWSE_OUTPUT) {
            if (const char* folder = SceneExtract::openOutputFolderDialog(outputFolder.data())) {
                settings->outputFolder = folder;
            }
        } else if (event.GetId() == IDC_BUTTON_BROWSE_BLENDER) {
            if (const char* file = SceneMesh::openSetBlenderFileDialog()) {
                settings->blenderPath = file;
            }
        } else if (event.GetId() == IDC_BUTTON_RESET_DEFAULTS) {
            resetDefaults(*settings);
        }
        setDialogInputs(dialog, *settings);
    });
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_SHOW_DEBUG_LOGS))
        ->Bind(wxEVT_CHECKBOX, [settings](wxCommandEvent& event) {
            settings->showDebugLogs = event.IsChecked();
            Logger::log(NK_INFO, "Show Debug logs set to %s.", settings->showDebugLogs ? "true" : "false");
        });
    const auto saveSettings = [this, settings] {
        backgroundColor = settings->backgroundColor;
        setHitmanFolder(settings->hitmanFolder);
        setOutputFolder(settings->outputFolder);
        setBlenderFile(settings->blenderPath);
        showDebugLogs = settings->showDebugLogs;
        PersistedSettings& persisted = PersistedSettings::getInstance();
        persisted.setValue("NavKit", "backgroundColor", std::to_string(backgroundColor));
        persisted.setValue("NavKit", "hitman", settings->hitmanFolder);
        persisted.setValue("NavKit", "output", settings->outputFolder);
        persisted.setValue("NavKit", "blender", settings->blenderPath);
        persisted.setValue("NavKit", "showDebugLogs", settings->showDebugLogs ? "true" : "false");
        persisted.save();
    };
    ok->Bind(wxEVT_BUTTON, [dialog, saveSettings](wxCommandEvent&) {
        saveSettings();
        dialog->Close();
    });
    apply->Bind(wxEVT_BUTTON, [saveSettings](wxCommandEvent&) { saveSettings(); });
    cancel->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) { dialog->Close(); });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hSettingsDialog == dialog) {
            hSettingsDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
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
        bool shouldReinitExtractionData = false;
        if (folderName != hitmanFolder) {
            shouldReinitExtractionData = true;
        }
        hitmanFolder = folderName;
        Logger::log(NK_INFO, ("Setting Hitman folder to: " + hitmanFolder).c_str());
        if (shouldReinitExtractionData) {
            Rpkg::backgroundWorker.emplace(&Rpkg::initExtractionData);
        }
    } else {
        Logger::log(NK_WARN, ("Could not find Hitman folder: " + folderName).c_str());
        shouldOpenSettingsDialog = true;
    }
}

void NavKitSettings::setOutputFolder(const std::string& folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        outputSet = true;
        outputFolder = folderName;
        Logger::log(NK_INFO, ("Setting output folder to: " + outputFolder).c_str());
    } else {
        Logger::log(NK_WARN, ("Could not find output folder: " + folderName).c_str());
        shouldOpenSettingsDialog = true;
    }
}

void NavKitSettings::setBlenderFile(const std::string& fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        blenderSet = true;
        blenderPath = fileName;
        Logger::log(NK_INFO, ("Setting Blender exe path to: " + blenderPath).c_str());
    } else {
        Logger::log(NK_WARN, ("Could not find Blender exe path: " + fileName).c_str());
        shouldOpenSettingsDialog = true;
    }
}
