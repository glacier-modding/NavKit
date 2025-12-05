#include "../../include/NavKit/util/UpdateChecker.h"
#include <shellapi.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <filesystem>
#include <fstream>
#include <httplib.h>
#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Logger.h"

UpdateChecker::UpdateChecker()
    : updateCheckCompleted(false)
      , isUpdateAvailable(false) {
}

UpdateChecker::~UpdateChecker() {
    if (updateThread.joinable()) {
        updateThread.join();
    }
};

void UpdateChecker::startUpdateCheck() {
    if (updateThread.joinable()) {
        return;
    }
    updateThread = std::thread(&UpdateChecker::performUpdateCheck);
}

void UpdateChecker::performUpdateCheck() {
    UpdateChecker &updateChecker = getInstance();
    Logger::log(NK_INFO, "Checking for updates.");

    httplib::Client cli("https://api.github.com");
    try {
        auto res = cli.Get("/repos/glacier-modding/NavKit/releases/latest");
        if (res == nullptr) {
            Logger::log(NK_ERROR, "GitHub API request failed");
            return;
        }
        if (res->status != 200) {
            Logger::log(NK_ERROR, "GitHub API request failed with status: %s", std::to_string(res->status));
            return;
        }
        updateChecker.responseBody = res->body;
        simdjson::ondemand::parser parser;
        simdjson::padded_string json(updateChecker.responseBody);
        simdjson::ondemand::document doc = parser.iterate(json);

        std::string_view latestVersionSV = doc["tag_name"];
        if (latestVersionSV.empty()) {
            Logger::log(NK_ERROR, "Error checking for updates.");
        }
        std::string latestVersionStr(latestVersionSV.substr(1));

        const std::string currentVersionStr =
                std::string(NavKit_VERSION_MAJOR) + "." + std::string(NavKit_VERSION_MINOR) + "." +
                std::string(NavKit_VERSION_PATCH);
        Logger::log(NK_INFO, ("Current version: " + currentVersionStr).c_str());
        Logger::log(NK_INFO, ("Latest version: " + latestVersionStr).c_str());
        bool updateAvailable = isVersionGreaterThan(latestVersionStr, currentVersionStr);
        Logger::log(NK_INFO, (std::string(updateAvailable ? "Update available." : "No update available.")).c_str());
        if (updateAvailable) {
            for (simdjson::ondemand::object asset: doc["assets"]) {
                if (std::string_view url_sv = asset["browser_download_url"]; url_sv.ends_with(".msi")) {
                    {
                        std::lock_guard lock(updateChecker.mutex);
                        updateChecker.latestVersion = "v" + latestVersionStr;
                        updateChecker.msiUrl = std::string(url_sv);
                        Logger::log(NK_INFO, ("MSI URL: " + std::string(url_sv)).c_str());
                        updateChecker.updateCheckCompleted = true;
                        updateChecker.isUpdateAvailable = true;
                        updateChecker.renderUpdatePopup();
                        return;
                    }
                }
            }
            Logger::log(NK_ERROR, "No MSI asset found in the latest GitHub release.");
        }
    } catch (const std::exception &e) {
        Logger::log(NK_ERROR, e.what());
    } {
        std::lock_guard lock(updateChecker.mutex);
        updateChecker.updateCheckCompleted = true;
    }
}

void UpdateChecker::renderUpdatePopup() {
    if (updateCheckCompleted && isUpdateAvailable) {
        openUpdateDialog("A new version is available: " + latestVersion + ". Would you like to update?");
    }
}

void splitUrl(const std::string &url, std::string &domain, std::string &path) {
    const size_t protocol_end = url.find("://");
    size_t start_pos = 0;

    if (protocol_end != std::string::npos) {
        start_pos = protocol_end + 3;
    }

    if (const size_t path_start = url.find('/', start_pos); path_start != std::string::npos) {
        domain = url.substr(start_pos, path_start - start_pos);
        path = url.substr(path_start);
    } else {
        domain = url.substr(start_pos);
        path = "/";
    }
}

void UpdateChecker::performUpdate() const {
    Logger::log(NK_INFO, "Preparing update...");

    if (msiUrl.empty()) {
        Logger::log(NK_ERROR, "NavKit: No MSI URL available for update.");
        return;
    }

    char current_exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, current_exe_path, MAX_PATH);
    const std::filesystem::path install_dir = std::filesystem::path(current_exe_path).parent_path();
    const std::filesystem::path original_updater_path = install_dir / "updater.exe";

    if (!std::filesystem::exists(original_updater_path)) {
        Logger::log(NK_ERROR, ("NavKit: updater.exe not found at " + original_updater_path.string()).c_str());
        return;
    }

    char temp_path_buf[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path_buf);
    const std::filesystem::path temp_updater_dir =
            std::filesystem::path(temp_path_buf) / ("NavKitUpdate_" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(temp_updater_dir);
    const std::filesystem::path temp_updater_path = temp_updater_dir / "updater.exe";

    try {
        std::filesystem::copy_file(original_updater_path, temp_updater_path,
                                   std::filesystem::copy_options::overwrite_existing);
        Logger::log(NK_INFO, ("Copied updater to " + temp_updater_path.string()).c_str());
    } catch (const std::filesystem::filesystem_error &e) {
        Logger::log(NK_ERROR, ("NavKit: Failed to copy updater to temp directory: " + std::string(e.what())).c_str());
        return;
    }

    const std::filesystem::path local_msi_path = std::filesystem::path(temp_path_buf) / "NavKit.msi";
    std::string domain, msi_path_part;
    splitUrl(msiUrl, domain, msi_path_part);
    httplib::Client cli("https://" + domain);
    cli.set_follow_location(true);

    Logger::log(NK_INFO, ("Downloading file: https://" + domain + msi_path_part).c_str());
    Logger::log(NK_INFO, ("Local path: " + local_msi_path.string()).c_str());

    if (auto res = cli.Get(msi_path_part); res && res->status == 200) {
        if (std::ofstream ofs(local_msi_path, std::ios::binary); ofs.is_open()) {
            ofs << res->body;
            ofs.close();
            Logger::log(NK_INFO, ("File downloaded successfully to " + local_msi_path.string()).c_str());
        } else {
            Logger::log(
                NK_ERROR, ("Error: Could not open local file " + local_msi_path.string() + " for writing.").c_str());
            return;
        }
    } else {
        Logger::log(
            NK_ERROR,
            ("Error: HTTP error occurred: " + (res ? std::to_string(res->status) : "Connection failure")).c_str());
        return;
    }

    const std::string command = "\"" + local_msi_path.string() + "\" "
                                + std::to_string(GetCurrentProcessId()) + " "
                                + latestVersion + " "
                                + "\"" + install_dir.string() + "\"";

    Logger::log(NK_INFO, ("Launching updater with command: " + command).c_str());
    if (reinterpret_cast<INT_PTR>(
            ShellExecuteA(nullptr, "open", temp_updater_path.string().c_str(), command.c_str(),
                          temp_updater_dir.string().c_str(), SW_SHOWNORMAL)) <= 32) {
        Logger::log(NK_ERROR, "NavKit: Failed to launch updater.exe from temp directory.");
        return;
    }
    Logger::log(NK_INFO, "Closing NavKit to allow update to proceed.");

    Sleep(1000);
    exit(0);
}

INT_PTR CALLBACK UpdateChecker::updateDialogHandler(const HWND hwndDlg, const UINT uMsg, const WPARAM wParam,
                                                    LPARAM lParam) {
    const UpdateChecker &updateChecker = getInstance();
    switch (uMsg) {
        case WM_INITDIALOG: {
            const HWND hStatic = GetDlgItem(hwndDlg, IDC_UPDATE_TEXT);
            SetWindowTextA(hStatic, updateChecker.updateMessage.c_str());
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDNO) {
                EndDialog(hwndDlg, IDNO);
                return TRUE;
            }
            if (LOWORD(wParam) == IDYES) {
                getInstance().performUpdate();
                EndDialog(hwndDlg, IDYES);
                return TRUE;
            }
            return FALSE;
        case WM_CLOSE:
            EndDialog(hwndDlg, IDOK);
            return TRUE;
        default: return FALSE;
    }
}

void UpdateChecker::openUpdateDialog(const std::string &message) {
    std::string formattedMessage = message;

    size_t pos = 0;
    while ((pos = formattedMessage.find('\n', pos)) != std::string::npos) {
        formattedMessage.replace(pos, 1, "\r\n");
        pos += 2;
    }
    updateMessage = std::string(formattedMessage);
    DialogBoxParamA(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_UPDATE_DIALOG), Renderer::hwnd, updateDialogHandler,
                    0);
}

bool UpdateChecker::isVersionGreaterThan(const std::string &v1, const std::string &v2) {
    std::vector<int> ver1, ver2;
    std::stringstream ss1(v1), ss2(v2);
    std::string segment;

    while (std::getline(ss1, segment, '.')) {
        ver1.push_back(std::stoi(segment));
    }
    while (std::getline(ss2, segment, '.')) {
        ver2.push_back(std::stoi(segment));
    }

    for (size_t i = 0; i < std::max(ver1.size(), ver2.size()); ++i) {
        const int n1 = i < ver1.size() ? ver1[i] : 0;
        const int n2 = i < ver2.size() ? ver2[i] : 0;
        if (n1 > n2) return true;
        if (n1 < n2) return false;
    }
    return false;
}
