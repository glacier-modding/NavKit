#pragma once

#include <cstdint>
#include <functional>
#include <wx/dialog.h>

class wxButton;
class wxRadioBox;
class wxStaticText;
class RegionListCtrl;

class CrmdCompareDialog : public wxDialog {
public:
    CrmdCompareDialog(wxWindow* parent, std::function<void()> onClosed);

    void update();

private:
    void refreshRegions();
    void selectAndFocus(long regionIndex);
    void step(int offset);
    long selectedRegion() const;

    std::function<void()> onClosed;
    wxRadioBox* viewChoice;
    wxStaticText* summary;
    RegionListCtrl* regionList;
    wxButton* previousButton;
    wxButton* nextButton;
    wxButton* goToButton;
    bool lastCanCompare = true;
    uint64_t shownDiffGeneration = 0;
};
