#pragma once

#include <functional>
#include <string>
#include <vector>
#include <wx/panel.h>

class wxListBox;
class wxTextCtrl;

class FilteredListPicker : public wxPanel {
public:
    FilteredListPicker(wxWindow* parent, wxWindowID id);

    void setItems(std::vector<std::string> items);

    [[nodiscard]] std::string getSelection() const;

    void onActivate(std::function<void()> callback);

private:
    void applyFilter();

    wxTextCtrl* filterField;
    wxListBox* list;
    std::vector<std::string> allItems;
    std::function<void()> activateCallback;
};
