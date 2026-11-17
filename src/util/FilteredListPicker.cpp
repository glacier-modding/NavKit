#include "../../include/NavKit/util/FilteredListPicker.h"

#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

FilteredListPicker::FilteredListPicker(wxWindow* parent, const wxWindowID id) : wxPanel(parent, id) {
    filterField = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    filterField->SetHint("Filter");
    list = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 300), 0, nullptr, wxLB_SINGLE);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(filterField, 0, wxEXPAND | wxBOTTOM, 5);
    sizer->Add(list, 1, wxEXPAND);
    SetSizer(sizer);

    filterField->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { applyFilter(); });
    filterField->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) {
        if (activateCallback && list->GetSelection() != wxNOT_FOUND) {
            activateCallback();
        }
    });
    list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) {
        if (activateCallback) {
            activateCallback();
        }
    });
}

void FilteredListPicker::setItems(std::vector<std::string> items) {
    allItems = std::move(items);
    applyFilter();
}

std::string FilteredListPicker::getSelection() const {
    const int selection = list->GetSelection();
    return selection == wxNOT_FOUND ? "" : list->GetString(selection).utf8_string();
}

void FilteredListPicker::onActivate(std::function<void()> callback) {
    activateCallback = std::move(callback);
}

void FilteredListPicker::applyFilter() {
    const wxString filter = filterField->GetValue().Lower();
    wxArrayString matches;
    for (const std::string& item : allItems) {
        const wxString display = wxString::FromUTF8(item);
        if (filter.empty() || display.Lower().Contains(filter)) {
            matches.Add(display);
        }
    }
    list->Freeze();
    list->Set(matches);
    if (!matches.empty()) {
        list->SetSelection(0);
    }
    list->Thaw();
}
