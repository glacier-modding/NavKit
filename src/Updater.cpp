#include <wx/app.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <wx/utils.h>

#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

wxDECLARE_EVENT(EVT_UPDATER_LOG, wxThreadEvent);
wxDECLARE_EVENT(EVT_UPDATER_COMPLETE, wxThreadEvent);
wxDEFINE_EVENT(EVT_UPDATER_LOG, wxThreadEvent);
wxDEFINE_EVENT(EVT_UPDATER_COMPLETE, wxThreadEvent);

struct UpdaterThreadArgs;
namespace {
    void runUpdater(std::unique_ptr<UpdaterThreadArgs> args);
}

class UpdaterFrame final : public wxFrame {
public:
    UpdaterFrame() : wxFrame(nullptr, wxID_ANY, "NavKit Updater", wxDefaultPosition, wxSize(500, 300)) {
        log = new wxTextCtrl(
            this, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
        closeButton = new wxButton(this, wxID_CLOSE, "Close");
        closeButton->Disable();
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(log, 1, wxEXPAND | wxALL, 10);
        sizer->Add(closeButton, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 10);
        SetSizer(sizer);
        Bind(EVT_UPDATER_LOG, &UpdaterFrame::onLog, this);
        Bind(EVT_UPDATER_COMPLETE, &UpdaterFrame::onUpdateComplete, this);
        closeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
        Bind(wxEVT_CLOSE_WINDOW, &UpdaterFrame::onClose, this);
    }

private:
    void onLog(wxThreadEvent& event) {
        log->AppendText(event.GetString() + "\n");
    }

    void onUpdateComplete(wxThreadEvent& event) {
        updateCompleted = true;
        if (event.GetInt() == 1) {
            Close(true);
        } else {
            closeButton->Enable();
            closeButton->SetFocus();
        }
    }

    void onClose(wxCloseEvent& event) {
        if (!updateCompleted) {
            event.Veto();
            return;
        }
        event.Skip();
    }

    wxTextCtrl* log = nullptr;
    wxButton* closeButton = nullptr;
    bool updateCompleted = false;
};

struct UpdaterThreadArgs {
    std::filesystem::path msi_path;
    long parent_pid = 0;
    std::string new_version_str;
    std::filesystem::path updater_exe_path;
    std::filesystem::path install_dir;
};

class UpdaterApp final : public wxApp {
public:
    bool OnInit() override {
        frame = new UpdaterFrame();
        SetTopWindow(frame);
        frame->Show();

        if (argc < 5) {
            wxMessageBox(
                "Updater: Invalid arguments.\n\nUsage: updater.exe <path_to_msi> <parent_process_id> <new_version> "
                "<install_dir>",
                "Argument Error", wxOK | wxICON_ERROR, frame);
            return false;
        }

        auto args = std::make_unique<UpdaterThreadArgs>();
        unsigned long parentPid = 0;
        if (!wxString(argv[2]).ToULong(&parentPid)) {
            wxMessageBox("Updater: Invalid parent process ID.", "Argument Error", wxOK | wxICON_ERROR, frame);
            return false;
        }

        args->msi_path = std::filesystem::u8path(wxString(argv[1]).ToUTF8().data());
        args->parent_pid = static_cast<long>(parentPid);
        args->new_version_str = wxString(argv[3]).ToStdString();
        args->updater_exe_path = std::filesystem::u8path(wxString(argv[0]).ToUTF8().data());
        args->install_dir = std::filesystem::u8path(wxString(argv[4]).ToUTF8().data());

        try {
            updateThread = std::thread([threadArgs = std::move(args)]() mutable { runUpdater(std::move(threadArgs)); });
        } catch (const std::exception& e) {
            wxMessageBox(wxString::FromUTF8("Failed to start updater thread: ") + e.what(), "Fatal Error",
                wxOK | wxICON_ERROR, frame);
            return false;
        }
        return true;
    }

    int OnExit() override {
        if (updateThread.joinable()) {
            updateThread.join();
        }
        return wxApp::OnExit();
    }

    UpdaterFrame* frame = nullptr;

private:
    std::thread updateThread;
};

wxIMPLEMENT_APP(UpdaterApp);

namespace {
    UpdaterFrame* updaterFrame() {
        return static_cast<UpdaterApp*>(wxTheApp)->frame;
    }

    wxString pathToWxString(const std::filesystem::path& path) {
        const std::u8string utf8Path = path.u8string();
        return wxString::FromUTF8(reinterpret_cast<const char*>(utf8Path.c_str()));
    }

    void LogMessage(const std::string& message) {
        if (updaterFrame()) {
            auto* event = new wxThreadEvent(EVT_UPDATER_LOG);
            event->SetString(wxString::FromUTF8(message));
            wxQueueEvent(updaterFrame(), event);
        }
    }

    void NotifyUpdateComplete(int status) {
        if (updaterFrame()) {
            auto* event = new wxThreadEvent(EVT_UPDATER_COMPLETE);
            event->SetInt(status);
            wxQueueEvent(updaterFrame(), event);
        }
    }

    long executeArguments(const std::vector<wxString>& arguments, int flags, wxProcess* process = nullptr,
        const wxExecuteEnv* environment = nullptr) {
        std::vector<const wxChar*> argv;
        argv.reserve(arguments.size() + 1);
        for (const wxString& argument : arguments) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);
        return wxExecute(argv.data(), flags, process, environment);
    }

    bool LaunchNavKit(const std::filesystem::path& navkit_path, const std::filesystem::path& install_dir) {
        wxProcess process;
        wxExecuteEnv environment;
        environment.cwd = pathToWxString(install_dir);
        const long processId = executeArguments({pathToWxString(navkit_path)}, wxEXEC_ASYNC, &process, &environment);
        if (processId == 0) {
            LogMessage("Updater: Failed to relaunch NavKit.");
            return false;
        }

        bool activated = false;
        for (int attempt = 0; attempt < 100 && wxProcess::Exists(static_cast<int>(processId)); ++attempt) {
            activated = process.Activate() || activated;
            if (activated) {
                break;
            }
            wxMilliSleep(100);
        }
        if (wxProcess::Exists(static_cast<int>(processId))) {
            process.Detach();
        }
        if (!activated) {
            LogMessage("Updater: NavKit started, but its window could not be brought to the foreground.");
        }
        return true;
    }

    void runUpdater(std::unique_ptr<UpdaterThreadArgs> args) {
        while (wxProcess::Exists(static_cast<int>(args->parent_pid))) {
            LogMessage("Updater: Waiting for NavKit to close (PID: " + std::to_string(args->parent_pid) + ")...");
            wxMilliSleep(100);
        }
        LogMessage("Updater: NavKit closed.");

        const std::filesystem::path& install_dir = args->install_dir;
        LogMessage("Updater: Target installation directory: " + install_dir.string());

        const std::filesystem::path log_path =
            install_dir / ("NavKit_Update_MSI_Log_" + args->new_version_str + ".txt");
        LogMessage("Updater: Executing MSI installer...");
        const long msiExitCode =
            executeArguments({"msiexec.exe", "/i", pathToWxString(args->msi_path), "/passive", "/norestart", "/L*v",
                                 pathToWxString(log_path), "MSIRESTARTMANAGERCONTROL=Disable"},
                wxEXEC_SYNC);
        if (msiExitCode < 0) {
            LogMessage("Updater: Failed to launch installer.");
            NotifyUpdateComplete(0);
            return;
        }
        LogMessage("Updater: MSI installation finished with exit code " + std::to_string(msiExitCode) + ".");

        const std::filesystem::path temp_settings_path = args->updater_exe_path.parent_path() / "NavKit.ini";
        const std::filesystem::path old_settings_destination_path = install_dir / "NavKit.ini.old";

        if (std::filesystem::exists(temp_settings_path)) {
            LogMessage("Updater: Staging previous NavKit.ini for merging by the application...");
            try {
                std::filesystem::copy_file(temp_settings_path, old_settings_destination_path,
                    std::filesystem::copy_options::overwrite_existing);
                LogMessage("Updater: Copied old settings to " + old_settings_destination_path.string());
            } catch (const std::exception& e) {
                LogMessage("Updater: Failed to stage old settings for merging. Error: " + std::string(e.what()));
            }
        } else {
            LogMessage("Updater: No previous NavKit.ini found to restore at " + temp_settings_path.string());
        }

        const std::filesystem::path navkit_path = install_dir / "NavKit.exe";
        int updateStatus = 0;
        if (msiExitCode != 0) {
            LogMessage("Updater: MSI installation failed. Check log for details: " + log_path.string());
            LogMessage("Updater: Attempting to relaunch the previous version of NavKit...");
            LaunchNavKit(navkit_path, install_dir);
        } else {
            LogMessage("Updater: MSI installation completed successfully.");
            if (std::error_code error; std::filesystem::remove(args->msi_path, error)) {
                LogMessage("Updater: Deleted downloaded MSI: " + args->msi_path.string());
            } else {
                LogMessage("Updater: Failed to delete MSI. Error: " + error.message());
            }

            LogMessage("Updater: Relaunching NavKit from: " + navkit_path.string());
            if (LaunchNavKit(navkit_path, install_dir)) {
                updateStatus = 1;
                wxExecuteEnv environment;
                environment.cwd = pathToWxString(install_dir);
                const long cleanupPid = executeArguments(
                    {pathToWxString(navkit_path), "--cleanup-update", std::to_string(wxGetProcessId()),
                        pathToWxString(args->updater_exe_path), pathToWxString(args->updater_exe_path.parent_path())},
                    wxEXEC_ASYNC, nullptr, &environment);
                if (cleanupPid == 0) {
                    LogMessage("Updater: Failed to schedule temporary updater cleanup.");
                } else {
                    LogMessage("Updater: Scheduled cleanup of temporary updater files.");
                }
            }
        }

        NotifyUpdateComplete(updateStatus);
    }
} // namespace
