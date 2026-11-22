#include "../../include/NavKit/module/DiffSettings.h"
#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <string>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

namespace {
    constexpr const char* settingsSection = "Diff";

    int readInt(const char* key, const int defaultValue) {
        const char* text =
            PersistedSettings::getInstance().getValue(settingsSection, key, std::to_string(defaultValue));
        int value = defaultValue;
        const auto [end, error] = std::from_chars(text, text + std::strlen(text), value);
        return error == std::errc() ? value : defaultValue;
    }

    void refreshCrmdDiff() {
        DiffSettings::getInstance().saveSettings();
        Crmd::getInstance().markDiffDirty();
    }
} // namespace

wxDialog* DiffSettings::hDiffSettingsDialog = nullptr;

void DiffSettings::loadSettings() {
    const CrmdDiff::Tolerances defaults;
    crmdTolerances.heightSteps = std::max(0, readInt("crmdHeightToleranceSteps", defaults.heightSteps));
    crmdTolerances.directionDegrees = std::max(0, readInt("crmdDirectionToleranceDegrees", defaults.directionDegrees));
    crmdKinds = static_cast<uint16_t>(readInt("crmdKinds", CrmdDiff::defaultKinds) & CrmdDiff::allKinds);
}

void DiffSettings::saveSettings() const {
    PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    persistedSettings.setValue(settingsSection, "crmdHeightToleranceSteps", std::to_string(crmdTolerances.heightSteps));
    persistedSettings.setValue(
        settingsSection, "crmdDirectionToleranceDegrees", std::to_string(crmdTolerances.directionDegrees));
    persistedSettings.setValue(settingsSection, "crmdKinds", std::to_string(crmdKinds));
    persistedSettings.save();
}

void DiffSettings::showDiffSettingsDialog() {
    if (hDiffSettingsDialog != nullptr) {
        hDiffSettingsDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Diff Settings");
    hDiffSettingsDialog = dialog;

    auto* crmdBox = new wxStaticBoxSizer(wxVERTICAL, dialog, "Crmd");
    wxWindow* crmdParent = crmdBox->GetStaticBox();
    auto* tolerances = new wxFlexGridSizer(2, 6, 10);
    auto* heightSpin = new wxSpinCtrl(crmdParent, IDC_SPIN_DIFF_CRMD_HEIGHT_TOLERANCE);
    heightSpin->SetRange(0, 10000);
    heightSpin->SetValue(crmdTolerances.heightSteps);
    heightSpin->SetToolTip("Grid point heights differ when they are more than this many height steps apart.");
    auto* directionSpin = new wxSpinCtrl(crmdParent, IDC_SPIN_DIFF_CRMD_DIRECTION_TOLERANCE);
    directionSpin->SetRange(0, 180);
    directionSpin->SetValue(crmdTolerances.directionDegrees);
    directionSpin->SetToolTip("Flow directions differ when they are more than this many degrees apart (3 per step).");
    tolerances->Add(new wxStaticText(crmdParent, wxID_ANY, "Height tolerance (steps):"), 0, wxALIGN_CENTER_VERTICAL);
    tolerances->Add(heightSpin, 1, wxEXPAND);
    tolerances->Add(
        new wxStaticText(crmdParent, wxID_ANY, "Direction tolerance (degrees):"), 0, wxALIGN_CENTER_VERTICAL);
    tolerances->Add(directionSpin, 1, wxEXPAND);
    crmdBox->Add(tolerances, 0, wxEXPAND | wxALL, 6);
    crmdBox->Add(new wxStaticText(crmdParent, wxID_ANY, "Differences to show:"), 0, wxLEFT | wxRIGHT | wxTOP, 6);

    auto* kinds = new wxFlexGridSizer(2, 4, 8);
    for (size_t k = 0; k < CrmdDiff::kindCount; ++k) {
        const glm::vec4 color = CrmdDiff::kindColor(k);
        auto* swatch = new wxPanel(crmdParent, wxID_ANY, wxDefaultPosition, wxSize(14, 14));
        swatch->SetBackgroundColour(wxColour(static_cast<unsigned char>(color.r * 255.0f),
            static_cast<unsigned char>(color.g * 255.0f), static_cast<unsigned char>(color.b * 255.0f)));
        auto* check =
            new wxCheckBox(crmdParent, IDC_CHECK_DIFF_CRMD_KIND_FIRST + static_cast<int>(k), CrmdDiff::kindName(k));
        const auto bit = static_cast<uint16_t>(1u << k);
        check->SetValue((crmdKinds & bit) != 0);
        check->Bind(wxEVT_CHECKBOX, [this, bit](const wxCommandEvent& event) {
            crmdKinds = event.IsChecked() ? crmdKinds | bit : crmdKinds & ~bit;
            refreshCrmdDiff();
        });
        kinds->Add(swatch, 0, wxALIGN_CENTER_VERTICAL);
        kinds->Add(check, 0, wxALIGN_CENTER_VERTICAL);
    }
    crmdBox->Add(kinds, 0, wxALL, 6);

    heightSpin->Bind(wxEVT_SPINCTRL, [this](const wxSpinEvent& event) {
        crmdTolerances.heightSteps = event.GetPosition();
        refreshCrmdDiff();
    });
    directionSpin->Bind(wxEVT_SPINCTRL, [this](const wxSpinEvent& event) {
        crmdTolerances.directionDegrees = event.GetPosition();
        refreshCrmdDiff();
    });

    auto* content = new wxBoxSizer(wxVERTICAL);
    content->Add(crmdBox, 0, wxEXPAND | wxALL, 10);
    dialog->SetSizerAndFit(content);
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hDiffSettingsDialog == dialog) {
            hDiffSettingsDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->CentreOnParent();
    dialog->Show();
}
