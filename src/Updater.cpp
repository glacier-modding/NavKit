#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>
#include <wx/thread.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <thread>

DWORD WINAPI UpdaterThread(LPVOID lpParam);

void LogMessage(const std::string& msg);

std::string ConvertWideToUTF8(const wchar_t* wstr);

wxDECLARE_EVENT(EVT_UPDATER_LOG, wxThreadEvent);
wxDECLARE_EVENT(EVT_UPDATER_COMPLETE, wxThreadEvent);
wxDEFINE_EVENT(EVT_UPDATER_LOG, wxThreadEvent);
wxDEFINE_EVENT(EVT_UPDATER_COMPLETE, wxThreadEvent);

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

    bool updateCompleted = false;

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

    wxTextCtrl* log;
    wxButton* closeButton;
};

class UpdaterApp final : public wxApp {
public:
    bool OnInit() override {
        frame = new UpdaterFrame();
        SetTopWindow(frame);
        frame->Show();
        return true;
    }

    UpdaterFrame* frame = nullptr;
};

wxIMPLEMENT_APP_NO_MAIN(UpdaterApp);
static UpdaterFrame* g_updaterFrame = nullptr;

struct MainWindowSearch {
    DWORD process_id;
    HWND window;
};

BOOL CALLBACK FindMainWindow(HWND hwnd, LPARAM lParam) {
    auto* search = reinterpret_cast<MainWindowSearch*>(lParam);
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id == search->process_id && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        search->window = hwnd;
        return FALSE;
    }
    return TRUE;
}

bool LaunchNavKit(const std::filesystem::path& navkit_path, const std::filesystem::path& install_dir) {
    const std::string executable = navkit_path.string();
    const std::string working_directory = install_dir.string();
    SHELLEXECUTEINFOA execute_info = {};
    execute_info.cbSize = sizeof(execute_info);
    execute_info.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute_info.lpVerb = "open";
    execute_info.lpFile = executable.c_str();
    execute_info.lpDirectory = working_directory.c_str();
    execute_info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExA(&execute_info)) {
        LogMessage("Updater: Failed to relaunch NavKit. Error: " + std::to_string(GetLastError()));
        return false;
    }
    if (execute_info.hProcess == nullptr) {
        LogMessage("Updater: NavKit started without a process handle; its window cannot be activated.");
        return true;
    }

    const DWORD process_id = GetProcessId(execute_info.hProcess);
    AllowSetForegroundWindow(process_id);

    MainWindowSearch search = {process_id, nullptr};
    for (int attempt = 0; attempt < 100 && search.window == nullptr; ++attempt) {
        EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&search));
        if (search.window == nullptr && WaitForSingleObject(execute_info.hProcess, 0) == WAIT_TIMEOUT) {
            Sleep(100);
        } else {
            break;
        }
    }

    bool activated = false;
    if (search.window != nullptr) {
        ShowWindow(search.window, SW_SHOW);
        activated = SetForegroundWindow(search.window) != FALSE;
    }
    CloseHandle(execute_info.hProcess);

    if (!activated) {
        LogMessage("Updater: NavKit started, but its window could not be brought to the foreground.");
    }
    return true;
}

struct UpdaterThreadArgs {
    std::filesystem::path msi_path;
    DWORD parent_pid;
    std::string new_version_str;
    std::filesystem::path updater_exe_path;
    std::filesystem::path install_dir;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argvW) {
        return 1;
    }
    if (!wxEntryStart(argc, argvW)) {
        LocalFree(argvW);
        return 1;
    }
    if (!wxTheApp->CallOnInit()) {
        LocalFree(argvW);
        wxEntryCleanup();
        return 1;
    }
    g_updaterFrame = static_cast<UpdaterApp*>(wxTheApp)->frame;
    if (argc < 5) {
        wxMessageBox(
            L"Updater: Invalid arguments.\n\nUsage: updater.exe <path_to_msi> <parent_process_id> <new_version> "
            L"<install_dir>",
            "Argument Error", wxOK | wxICON_ERROR, g_updaterFrame);
        if (argvW) {
            LocalFree(argvW);
        }
        wxEntryCleanup();
        return 1;
    }

    auto args = std::make_unique<UpdaterThreadArgs>();
    try {
        args->msi_path = argvW[1];
        args->parent_pid = std::stoul(argvW[2]);
        args->new_version_str = ConvertWideToUTF8(argvW[3]);
        args->install_dir = argvW[4];
        args->updater_exe_path = argvW[0];
    } catch (const std::exception& e) {
        std::string error_msg_str = "Updater: Failed to parse arguments. Error: ";
        error_msg_str += e.what();
        wxMessageBox(wxString::FromUTF8(error_msg_str), "Argument Error", wxOK | wxICON_ERROR, g_updaterFrame);
        LocalFree(argvW);
        wxEntryCleanup();
        return 1;
    }
    LocalFree(argvW);
    std::thread updateThread([threadArgs = args.release()] { UpdaterThread(threadArgs); });
    if (!updateThread.joinable()) {
        wxMessageBox("Failed to start updater thread.", "Fatal Error", wxOK | wxICON_ERROR, g_updaterFrame);
        wxEntryCleanup();
        return 1;
    }
    const int result = wxTheApp->OnRun();
    updateThread.join();
    wxTheApp->OnExit();
    wxEntryCleanup();
    return result;
}

std::string ConvertWideToUTF8(const wchar_t* wstr) {
    if (wstr == nullptr || wstr[0] == L'\0') {
        return std::string();
    }
    const int wstr_len = static_cast<int>(wcslen(wstr));
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, wstr_len, nullptr, 0, nullptr, nullptr);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, wstr_len, &strTo[0], size_needed, nullptr, nullptr);
    return strTo;
}

void LogMessage(const std::string& msg) {
    if (g_updaterFrame) {
        auto* event = new wxThreadEvent(EVT_UPDATER_LOG);
        event->SetString(wxString::FromUTF8(msg));
        wxQueueEvent(g_updaterFrame, event);
    }
}

void NotifyUpdateComplete(const WPARAM status) {
    if (g_updaterFrame) {
        auto* event = new wxThreadEvent(EVT_UPDATER_COMPLETE);
        event->SetInt(static_cast<int>(status));
        wxQueueEvent(g_updaterFrame, event);
    }
}

DWORD WINAPI UpdaterThread(LPVOID lpParam) {
    std::unique_ptr<UpdaterThreadArgs> args(static_cast<UpdaterThreadArgs*>(lpParam));

    if (const HANDLE parent_handle = OpenProcess(SYNCHRONIZE, FALSE, args->parent_pid)) {
        LogMessage("Updater: Waiting for NavKit to close (PID: " + std::to_string(args->parent_pid) + ")...");
        WaitForSingleObject(parent_handle, INFINITE);
        CloseHandle(parent_handle);
        LogMessage("Updater: NavKit closed.");
    } else {
        LogMessage("Updater: Could not open parent process handle (PID: " + std::to_string(args->parent_pid) +
            "). Continuing anyway.");
    }

    const std::filesystem::path& install_dir = args->install_dir;
    LogMessage("Updater: Target installation directory: " + install_dir.string());

    const std::filesystem::path log_path = install_dir / ("NavKit_Update_MSI_Log_" + args->new_version_str + ".txt");

    std::string msi_args = "/i \"" + args->msi_path.string() + "\" /passive /norestart /L*v \"" + log_path.string() +
        "\" MSIRESTARTMANAGERCONTROL=Disable";
    std::string command_line_str = "msiexec.exe " + msi_args;
    std::vector command_line_buf(command_line_str.begin(), command_line_str.end());
    command_line_buf.push_back('\0');

    LogMessage("Updater: Executing MSI installer...");

    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(nullptr, command_line_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        LogMessage("Updater: Failed to launch installer. Error: " + std::to_string(GetLastError()));
        NotifyUpdateComplete(0);
        return 1;
    }
    LogMessage("Updater: Waiting for MSI installation to complete...");
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD msi_exit_code;
    GetExitCodeProcess(pi.hProcess, &msi_exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    const std::filesystem::path temp_settings_path = args->updater_exe_path.parent_path() / "NavKit.ini";
    const std::filesystem::path old_settings_destination_path = install_dir / "NavKit.ini.old";

    if (std::filesystem::exists(temp_settings_path)) {
        LogMessage("Updater: Staging previous NavKit.ini for merging by the application...");
        LogMessage("Updater: Previous settings source: " + temp_settings_path.string());
        LogMessage("Updater: Previous settings destination: " + old_settings_destination_path.string());
        try {
            std::filesystem::copy_file(
                temp_settings_path, old_settings_destination_path, std::filesystem::copy_options::overwrite_existing);
            LogMessage("Updater: Copied old settings to " + old_settings_destination_path.string());
        } catch (const std::exception& e) {
            LogMessage("Updater: Failed to stage old settings for merging. Error: " + std::string(e.what()));
        }
    } else {
        LogMessage("Updater: No previous NavKit.ini found to restore at " + temp_settings_path.string());
    }

    const std::filesystem::path navkit_path = install_dir / "NavKit.exe";
    WPARAM update_status = 0;

    if (msi_exit_code != 0) {
        LogMessage("Updater: MSI installation failed with exit code: " + std::to_string(msi_exit_code));
        LogMessage("Updater: Check log for details: " + log_path.string());
        LogMessage("Updater: Attempting to relaunch the previous version of NavKit...");
        LaunchNavKit(navkit_path, install_dir);
    } else {
        LogMessage("Updater: MSI installation completed successfully.");
        if (std::error_code ec; std::filesystem::remove(args->msi_path, ec)) {
            LogMessage("Updater: Deleted downloaded MSI: " + args->msi_path.string());
        } else {
            LogMessage("Updater: Failed to delete MSI. Error: " + ec.message());
        }

        LogMessage("Updater: Relaunching NavKit from: " + navkit_path.string());
        if (LaunchNavKit(navkit_path, install_dir)) {
            LogMessage("Updater: Scheduling self-deletion of temporary files.");
            std::string temp_updater_path_str = args->updater_exe_path.string();
            std::string temp_dir_path_str = args->updater_exe_path.parent_path().string();
            std::string self_delete_cmd =
                "cmd.exe /C start /B \"\" cmd /C \"ping 127.0.0.1 -n 4 > nul && del /Q /F \"" + temp_updater_path_str +
                "\" && rmdir \"" + temp_dir_path_str + "\"\"";

            STARTUPINFOA si_del = {sizeof(STARTUPINFOA)};
            PROCESS_INFORMATION pi_del = {};
            if (CreateProcessA(nullptr, &self_delete_cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                    nullptr, &si_del, &pi_del)) {
                CloseHandle(pi_del.hProcess);
                CloseHandle(pi_del.hThread);
            } else {
                LogMessage("Updater: Failed to schedule self-deletion. Error: " + std::to_string(GetLastError()));
            }

            update_status = 1;
        }
    }

    NotifyUpdateComplete(update_status);
    return 0;
}
