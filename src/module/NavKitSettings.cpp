#include "../../include/NavKit/module/NavKitSettings.h"

#include <filesystem>
#include <charconv>
#include <limits>
#include <wx/spinctrl.h>
#include "../../include/NavKit/util/Threading.h"
#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <wx/stdpaths.h>
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

namespace {
    std::string wxPathToUtf8(const wxString& path) {
        const wxScopedCharBuffer utf8 = path.ToUTF8();
        return utf8 ? utf8.data() : "";
    }

    std::string defaultOutputFolder() {
        std::string baseDirectory = wxPathToUtf8(wxStandardPaths::Get().GetDocumentsDir());
        if (baseDirectory.empty()) {
            baseDirectory = wxPathToUtf8(wxStandardPaths::Get().GetUserDataDir());
        }
        const std::filesystem::path outputPath = std::filesystem::u8path(baseDirectory) / "NavKitOutput";
        std::error_code error;
        std::filesystem::create_directories(outputPath, error);
        if (error) {
            Logger::log(NK_WARN, "Could not create default output folder: %s", error.message().c_str());
        }
        return outputPath.string();
    }
} // namespace

void NavKitSettings::resetDefaults(DialogSettings& settings) {
    settings.backgroundColor = 0.16f;
    settings.hitmanFolder.clear();
    settings.outputFolder = defaultOutputFolder();
    settings.blenderPath.clear();
    settings.showDebugLogs = false;
    settings.darkMode = true;
    settings.maxThreads = Threading::defaultMaxThreads();
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
#ifdef __WXMSW__
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_DARK_MODE))->SetValue(tempSettings.darkMode);
#endif
    static_cast<wxSpinCtrl*>(dialog->FindWindow(IDC_SPIN_MAX_THREADS))->SetValue(tempSettings.maxThreads);
}

NavKitSettings::NavKitSettings() :
    backgroundColor(0.30f), hitmanSet(false), outputSet(false), blenderSet(false), showDebugLogs(false), darkMode(true),
    shouldOpenSettingsDialog(false) {}

void NavKitSettings::showNavKitSettingsDialog() {
    if (hSettingsDialog) {
        hSettingsDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "NavKit Settings", wxDefaultPosition, wxSize(640, 380));
    hSettingsDialog = dialog;
    auto settings = std::make_shared<DialogSettings>(DialogSettings{
        backgroundColor, hitmanFolder, outputFolder, blenderPath, showDebugLogs, Threading::getMaxThreads(), darkMode});
    auto* layout = new wxFlexGridSizer(0, 3, 10, 8);
#ifdef __WXMSW__
    layout->Add(new wxStaticText(dialog, wxID_ANY, "Appearance:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* darkModeCheck = new wxCheckBox(dialog, IDC_CHECK_DARK_MODE, "Dark mode (requires restart)");
    layout->Add(darkModeCheck, 0, wxALIGN_CENTER_VERTICAL);
    layout->AddSpacer(1);
    darkModeCheck->Bind(wxEVT_CHECKBOX, [settings](wxCommandEvent& event) { settings->darkMode = event.IsChecked(); });
#endif
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
    layout->Add(new wxStaticText(dialog, wxID_ANY, "Maximum Worker Threads:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* threads = new wxSpinCtrl(dialog, IDC_SPIN_MAX_THREADS);
    threads->SetRange(1, std::numeric_limits<int>::max());
    threads->SetToolTip("Maximum threads per parallel operation. 1 runs work sequentially. Applies to new operations.");
    layout->Add(threads, 1, wxEXPAND);
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
    threads->Bind(wxEVT_SPINCTRL,
        [settings](wxSpinEvent& event) { settings->maxThreads = static_cast<unsigned int>(event.GetValue()); });
    threads->Bind(wxEVT_TEXT, [settings, threads](wxCommandEvent&) {
        settings->maxThreads = static_cast<unsigned int>(threads->GetValue());
    });
    const auto saveSettings = [this, settings, threads] {
        settings->maxThreads = static_cast<unsigned int>(threads->GetValue());
        Threading::setMaxThreads(settings->maxThreads);
        backgroundColor = settings->backgroundColor;
        setHitmanFolder(settings->hitmanFolder);
        setOutputFolder(settings->outputFolder);
        setBlenderFile(settings->blenderPath);
        showDebugLogs = settings->showDebugLogs;
        darkMode = settings->darkMode;
        PersistedSettings& persisted = PersistedSettings::getInstance();
        persisted.setValue("NavKit", "backgroundColor", std::to_string(backgroundColor));
        persisted.setValue("NavKit", "hitman", settings->hitmanFolder);
        persisted.setValue("NavKit", "output", settings->outputFolder);
        persisted.setValue("NavKit", "blender", settings->blenderPath);
        persisted.setValue("NavKit", "showDebugLogs", settings->showDebugLogs ? "true" : "false");
        persisted.setValue("NavKit", "maxThreads", std::to_string(settings->maxThreads));
        persisted.setValue("NavKit", "darkMode", darkMode ? "true" : "false");
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
    const std::string threadSetting = persistedSettings.getValue("NavKit", "maxThreads", "");
    unsigned int threadCount = Threading::defaultMaxThreads();
    unsigned int parsed = 0;
    const auto result = std::from_chars(threadSetting.data(), threadSetting.data() + threadSetting.size(), parsed);
    if (result.ec == std::errc{} && result.ptr == threadSetting.data() + threadSetting.size() && parsed > 0 &&
        parsed <= static_cast<unsigned int>(std::numeric_limits<int>::max()))
        threadCount = parsed;
    Threading::setMaxThreads(threadCount);
    backgroundColor = static_cast<float>(atof(persistedSettings.getValue("NavKit", "backgroundColor", "0.16f")));
    const std::string hitmanFolder = persistedSettings.getValue("NavKit", "hitman", "");
    const std::string outputFolder = persistedSettings.getValue("NavKit", "output", defaultOutputFolder());
    const std::string blenderPath = persistedSettings.getValue("NavKit", "blender", "");
    setHitmanFolder(hitmanFolder);
    setOutputFolder(outputFolder);
    setBlenderFile(blenderPath);
    showDebugLogs = strcmp(persistedSettings.getValue("NavKit", "showDebugLogs", "false"), "true") == 0;
    darkMode = strcmp(persistedSettings.getValue("NavKit", "darkMode", "true"), "true") == 0;
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
