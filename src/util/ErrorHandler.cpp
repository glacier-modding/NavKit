#include "../../include/NavKit/util/ErrorHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <wx/clipbrd.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/thread.h>

void ErrorHandler::openErrorDialog(const std::string& message) {
    if (!wxTheApp) {
        Logger::log(NK_ERROR, "%s", message.c_str());
        return;
    }
    if (!wxIsMainThread()) {
        wxTheApp->CallAfter([message] { openErrorDialog(message); });
        return;
    }
    wxDialog dialog(getMainFrame(), wxID_ANY, "Error", wxDefaultPosition, wxSize(650, 500));
    auto* text = new wxTextCtrl(&dialog, wxID_ANY, wxString::FromUTF8(message), wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE | wxTE_READONLY);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* copyButton = new wxButton(&dialog, wxID_COPY, "Copy");
    auto* okButton = new wxButton(&dialog, wxID_OK, "OK");
    buttons->Add(copyButton, 0, wxRIGHT, 8);
    buttons->Add(okButton);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND | wxALL, 10);
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxRIGHT | wxBOTTOM, 10);
    dialog.SetSizer(sizer);
    copyButton->Bind(wxEVT_BUTTON, [&message](wxCommandEvent&) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(message)));
            wxTheClipboard->Close();
        }
    });
    okButton->Bind(wxEVT_BUTTON, [&dialog](wxCommandEvent&) { dialog.EndModal(wxID_OK); });
    dialog.SetDefaultItem(okButton);
    dialog.Layout();
    dialog.CentreOnParent();
    dialog.ShowModal();
}
