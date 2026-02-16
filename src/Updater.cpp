#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>

HWND g_hMainWnd = nullptr;
HWND g_hEditLog = nullptr;
HWND g_hCloseButton = nullptr;

#define WM_APP_LOG_MESSAGE      (WM_APP + 1)
#define WM_APP_UPDATE_COMPLETE  (WM_APP + 2)

#define IDC_EDIT_LOG            101
#define IDC_CLOSE_BUTTON        102

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

DWORD WINAPI UpdaterThread(LPVOID lpParam);

void LogMessage(const std::string& msg);

std::string ConvertWideToUTF8(const wchar_t* wstr);

struct UpdaterThreadArgs {
    std::filesystem::path msi_path;
    DWORD parent_pid;
    std::string new_version_str;
    std::filesystem::path updater_exe_path;
    std::filesystem::path install_dir;
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    int argc;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argvW == nullptr || argc < 5) {
        MessageBoxW(
            nullptr,
            L"Updater: Invalid arguments.\n\nUsage: updater.exe <path_to_msi> <parent_process_id> <new_version> <install_dir>",
            L"Argument Error", MB_OK | MB_ICONERROR);
        if (argvW) {
            LocalFree(argvW);
        }
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
        const std::wstring error_msg_wstr(error_msg_str.begin(), error_msg_str.end());
        MessageBoxW(nullptr, error_msg_wstr.c_str(), L"Argument Error", MB_OK | MB_ICONERROR);
        LocalFree(argvW);
        return 1;
    }
    LocalFree(argvW);

    constexpr wchar_t CLASS_NAME[] = L"NavKitUpdaterWindowClass";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>((COLOR_WINDOW + 1));
    RegisterClassW(&wc);

    g_hMainWnd = CreateWindowExW(
        0, CLASS_NAME, L"NavKit Updater",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 300,
        nullptr, nullptr, hInstance, nullptr);

    if (g_hMainWnd == nullptr) {
        return 0;
    }

    const HANDLE hThread = CreateThread(nullptr, 0, UpdaterThread, args.release(), 0, nullptr);
    if (hThread == nullptr) {
        MessageBoxW(g_hMainWnd, L"Failed to start updater thread.", L"Fatal Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(hThread);

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hEditLog = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            10, 10, 465, 200, hwnd, reinterpret_cast<HMENU>(IDC_EDIT_LOG), GetModuleHandle(nullptr), nullptr);

        g_hCloseButton = CreateWindowExW(
            0, L"BUTTON", L"Close",
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | WS_DISABLED,
            385, 220, 90, 25, hwnd, reinterpret_cast<HMENU>(IDC_CLOSE_BUTTON), GetModuleHandle(nullptr), nullptr);

        auto hFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessage(g_hEditLog, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        SendMessage(g_hCloseButton, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
        return 0;
    }
    case WM_APP_LOG_MESSAGE: {
        char* msg = reinterpret_cast<char*>(lParam);
        const int len = GetWindowTextLength(g_hEditLog);
        SendMessage(g_hEditLog, EM_SETSEL, len, len);
        SendMessageA(g_hEditLog, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(msg));
        SendMessageA(g_hEditLog, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>("\r\n"));
        delete[] msg;
        return 0;
    }
    case WM_APP_UPDATE_COMPLETE: {
        if (wParam == 1) {
            DestroyWindow(hwnd);
        } else {
            EnableWindow(g_hCloseButton, TRUE);
            SetFocus(g_hCloseButton);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_CLOSE_BUTTON) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default: ;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
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
    const size_t len = msg.length() + 1;
    auto msg_copy = new char[len];
    strcpy_s(msg_copy, len, msg.c_str());
    PostMessage(g_hMainWnd, WM_APP_LOG_MESSAGE, 0, reinterpret_cast<LPARAM>(msg_copy));
}

DWORD WINAPI UpdaterThread(LPVOID lpParam) {
    std::unique_ptr<UpdaterThreadArgs> args(static_cast<UpdaterThreadArgs*>(lpParam));

    if (const HANDLE parent_handle = OpenProcess(SYNCHRONIZE, FALSE, args->parent_pid)) {
        LogMessage("Updater: Waiting for NavKit to close (PID: " + std::to_string(args->parent_pid) + ")...");
        WaitForSingleObject(parent_handle, INFINITE);
        CloseHandle(parent_handle);
        LogMessage("Updater: NavKit closed.");
    } else {
        LogMessage(
            "Updater: Could not open parent process handle (PID: " + std::to_string(args->parent_pid) +
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
        PostMessage(g_hMainWnd, WM_APP_UPDATE_COMPLETE, 0, 0);
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
        try {
            std::filesystem::copy_file(temp_settings_path, old_settings_destination_path,
                                       std::filesystem::copy_options::overwrite_existing);
            LogMessage("Updater: Copied old settings to " + old_settings_destination_path.string());
        } catch (const std::exception& e) {
            LogMessage("Updater: Failed to stage old settings for merging. Error: " + std::string(e.what()));
        }
    } else {
        LogMessage("Updater: No previous NavKit.ini found to restore.");
    }

    const std::filesystem::path navkit_path = install_dir / "NavKit.exe";
    WPARAM update_status = 0;

    if (msi_exit_code != 0) {
        LogMessage("Updater: MSI installation failed with exit code: " + std::to_string(msi_exit_code));
        LogMessage("Updater: Check log for details: " + log_path.string());
        LogMessage("Updater: Attempting to relaunch the previous version of NavKit...");
        ShellExecuteA(nullptr, "open", navkit_path.string().c_str(),
                      nullptr, install_dir.string().c_str(), SW_SHOWNORMAL);
    } else {
        LogMessage("Updater: MSI installation completed successfully.");
        if (std::error_code ec; std::filesystem::remove(args->msi_path, ec)) {
            LogMessage("Updater: Deleted downloaded MSI: " + args->msi_path.string());
        } else {
            LogMessage("Updater: Failed to delete MSI. Error: " + ec.message());
        }

        LogMessage("Updater: Relaunching NavKit from: " + navkit_path.string());
        if (reinterpret_cast<INT_PTR>(ShellExecuteA(nullptr, "open", navkit_path.string().c_str(), nullptr,
                                                    install_dir.string().c_str(), SW_SHOWNORMAL)) <= 32) {
            LogMessage("Updater: Failed to relaunch NavKit. Error: " + std::to_string(GetLastError()));
        } else {
            LogMessage("Updater: Scheduling self-deletion of temporary files.");
            std::string temp_updater_path_str = args->updater_exe_path.string();
            std::string temp_dir_path_str = args->updater_exe_path.parent_path().string();
            std::string self_delete_cmd = "cmd.exe /C start /B \"\" cmd /C \"ping 127.0.0.1 -n 4 > nul && del /Q /F \""
                + temp_updater_path_str + "\" && rmdir \"" + temp_dir_path_str + "\"\"";

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

    PostMessage(g_hMainWnd, WM_APP_UPDATE_COMPLETE, update_status, 0);
    return 0;
}
