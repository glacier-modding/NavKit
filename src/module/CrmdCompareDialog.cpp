#include "../../include/NavKit/module/CrmdCompareDialog.h"
#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/module/DiffSettings.h"
#include "../../include/NavKit/util/CrmdDiff.h"
#include <array>
#include <cstdio>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

class RegionListCtrl : public wxListCtrl {
public:
    explicit RegionListCtrl(wxWindow* parent) :
        wxListCtrl(
            parent, wxID_ANY, wxDefaultPosition, wxSize(560, 300), wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL) {
        AppendColumn("#", wxLIST_FORMAT_RIGHT, 50);
        AppendColumn("Cells", wxLIST_FORMAT_RIGHT, 60);
        AppendColumn("Centre (x, y, z)", wxLIST_FORMAT_LEFT, 200);
        AppendColumn("Differences", wxLIST_FORMAT_LEFT, 240);
    }

private:
    wxString OnGetItemText(const long item, const long column) const override {
        const Crmd& crmd = Crmd::getInstance();
        if (item < 0 || static_cast<size_t>(item) >= crmd.diffRegions.size()) {
            return {};
        }
        const CrmdDiff::Region& region = crmd.diffRegions[item];
        switch (column) {
        case 0:
            return wxString::Format("%ld", item + 1);
        case 1:
            return wxString::Format("%zu", region.cellCount);
        case 2: {
            const glm::dvec3 centre = CrmdDiff::gridToWorld(
                *crmd.crowdMapData, (region.minX + region.maxX + 1) * 0.5, (region.minY + region.maxY + 1) * 0.5);
            return wxString::Format("%.2f, %.2f, %.2f", centre.x, centre.y, centre.z);
        }
        default:
            return CrmdDiff::describeKinds(region.kinds);
        }
    }
};

CrmdCompareDialog::CrmdCompareDialog(wxWindow* parent, std::function<void()> onClosed) :
    wxDialog(
        parent, wxID_ANY, "Crmd Compare", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
    onClosed(std::move(onClosed)) {
    const std::array<wxString, 3> viewNames = {"Loaded", "Comparison", "Differences"};
    viewChoice = new wxRadioBox(this, wxID_ANY, "Show", wxDefaultPosition, wxDefaultSize,
        static_cast<int>(viewNames.size()), viewNames.data(), 1, wxRA_SPECIFY_ROWS);
    viewChoice->SetToolTip("Press C in the viewport to flip between loaded and comparison.");
    summary = new wxStaticText(this, wxID_ANY, "");
    regionList = new RegionListCtrl(this);
    previousButton = new wxButton(this, wxID_ANY, "Previous");
    nextButton = new wxButton(this, wxID_ANY, "Next");
    goToButton = new wxButton(this, wxID_ANY, "Go To");
    auto* settingsButton = new wxButton(this, wxID_ANY, "Diff Settings...");

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(previousButton, 0, wxRIGHT, 6);
    buttons->Add(nextButton, 0, wxRIGHT, 6);
    buttons->Add(goToButton, 0, wxRIGHT, 6);
    buttons->AddStretchSpacer();
    buttons->Add(settingsButton);

    auto* content = new wxBoxSizer(wxVERTICAL);
    content->Add(viewChoice, 0, wxEXPAND | wxALL, 10);
    content->Add(summary, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    content->Add(regionList, 1, wxEXPAND | wxALL, 10);
    content->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    SetSizerAndFit(content);

    viewChoice->Bind(wxEVT_RADIOBOX, [](const wxCommandEvent& event) {
        Crmd::getInstance().setCompareView(static_cast<Crmd::CompareView>(event.GetSelection()));
    });
    regionList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](const wxListEvent& event) { selectAndFocus(event.GetIndex()); });
    previousButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { step(-1); });
    nextButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { step(1); });
    goToButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { selectAndFocus(selectedRegion()); });
    settingsButton->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { DiffSettings::getInstance().showDiffSettingsDialog(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
        this->onClosed();
        Destroy();
    });

    lastCanCompare = !Crmd::getInstance().canCompare();
    shownDiffGeneration = Crmd::getInstance().diffGeneration - 1;
    update();
    CentreOnParent();
}

void CrmdCompareDialog::update() {
    Crmd& crmd = Crmd::getInstance();
    const bool canCompare = crmd.canCompare();
    if (canCompare != lastCanCompare) {
        lastCanCompare = canCompare;
        viewChoice->Enable(canCompare);
        regionList->Enable(canCompare);
        if (!canCompare) {
            regionList->SetItemCount(0);
            summary->SetLabel("Load a Crmd and a comparison Crmd to compare.");
            previousButton->Disable();
            nextButton->Disable();
            goToButton->Disable();
        }
    }
    if (const int view = static_cast<int>(crmd.compareView); viewChoice->GetSelection() != view) {
        viewChoice->SetSelection(view);
    }
    if (!canCompare) {
        return;
    }
    crmd.refreshDiff();
    if (crmd.diffGeneration != shownDiffGeneration) {
        shownDiffGeneration = crmd.diffGeneration;
        refreshRegions();
    }
}

void CrmdCompareDialog::refreshRegions() {
    const Crmd& crmd = Crmd::getInstance();
    const size_t regionCount = crmd.diffRegions.size();
    regionList->SetItemCount(static_cast<long>(regionCount));
    regionList->Refresh();
    wxString text;
    if (!crmd.diffResult.gridsMatch) {
        text = "Grid sizes differ, so cells were not compared.";
    } else {
        text = wxString::Format("%zu regions of differences", regionCount);
    }
    if (!crmd.diffResult.header.empty()) {
        text += wxString::Format(" (%zu header differences, see log)", crmd.diffResult.header.size());
    }
    summary->SetLabel(text);
    previousButton->Enable(regionCount > 0);
    nextButton->Enable(regionCount > 0);
    goToButton->Enable(regionCount > 0);
}

long CrmdCompareDialog::selectedRegion() const {
    return regionList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

void CrmdCompareDialog::selectAndFocus(const long regionIndex) {
    if (regionIndex < 0 || regionIndex >= regionList->GetItemCount()) {
        return;
    }
    if (const long previous = selectedRegion(); previous != regionIndex && previous >= 0) {
        regionList->SetItemState(previous, 0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
    regionList->SetItemState(
        regionIndex, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    regionList->EnsureVisible(regionIndex);
    Crmd::getInstance().focusOnDiffRegion(static_cast<size_t>(regionIndex));
}

void CrmdCompareDialog::step(const int offset) {
    const long count = regionList->GetItemCount();
    if (count == 0) {
        return;
    }
    const long current = selectedRegion();
    const long next = current < 0 ? (offset > 0 ? 0 : count - 1) : ((current + offset) % count + count) % count;
    selectAndFocus(next);
}
