#include "../../include/NavKit/util/UpdateChecker.h"
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <simdjson.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/NavKit/util/FileUtil.h"
#include <wx/msgdlg.h>

namespace {
    bool isPlatformReleaseAsset(const std::string_view url) {
#ifdef _WIN32
        return url.ends_with(".msi");
#elif defined(__APPLE__)
#if defined(__arm64__) || defined(__aarch64__)
        return url.ends_with("NavKit-macos-arm64.dmg");
#elif defined(__x86_64__)
        return url.ends_with("NavKit-macos-x86_64.dmg");
#else
        return false;
#endif
#else
        return false;
#endif
    }

    wxString pathToWxString(const std::filesystem::path& path) {
        const std::u8string utf8Path = path.u8string();
        return wxString::FromUTF8(reinterpret_cast<const char*>(utf8Path.c_str()));
    }

    long executeArguments(const std::vector<wxString>& arguments, int flags, const wxExecuteEnv* environment) {
        std::vector<const wxChar*> argv;
        argv.reserve(arguments.size() + 1);
        for (const wxString& argument : arguments) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);
        return wxExecute(argv.data(), flags, nullptr, environment);
    }
} // namespace

UpdateChecker::UpdateChecker() : updateCheckCompleted(false), isUpdateAvailable(false) {}

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

void UpdateChecker::waitForUpdateCheck() {
    if (updateThread.joinable()) {
        updateThread.join();
    }
}

void UpdateChecker::performUpdateCheck() {
    UpdateChecker& updateChecker = getInstance();
    Logger::log(NK_INFO, "Checking for updates.");

    httplib::Client cli("https://api.github.com");
    try {
        auto res = cli.Get("/repos/glacier-modding/NavKit/releases/latest");
        if (res == nullptr) {
            Logger::log(NK_ERROR, "GitHub API request failed");
            return;
        }
        if (res->status != 200) {
            Logger::log(NK_ERROR, "GitHub API request failed with status: %s", std::to_string(res->status).c_str());
            return;
        }
        updateChecker.responseBody = res->body;
        simdjson::ondemand::parser parser;
        const simdjson::padded_string json(updateChecker.responseBody);
        simdjson::ondemand::document doc = parser.iterate(json);

        const std::string_view latestVersionSV = doc["tag_name"];
        if (latestVersionSV.empty()) {
            Logger::log(NK_ERROR, "Error checking for updates.");
        }
        const std::string latestVersionStr(latestVersionSV.substr(1));

        const std::string currentVersionStr = std::string(NavKit_VERSION_MAJOR) + "." +
            std::string(NavKit_VERSION_MINOR) + "." + std::string(NavKit_VERSION_PATCH);
        Logger::log(NK_INFO, ("Current NavKit version: " + currentVersionStr).c_str());
        Logger::log(NK_INFO, ("Latest NavKit version: " + latestVersionStr).c_str());
        const bool updateAvailable = isVersionGreaterThan(latestVersionStr, currentVersionStr);
        Logger::log(NK_INFO, std::string(updateAvailable ? "Update available." : "No update available.").c_str());
        if (updateAvailable) {
            for (simdjson::ondemand::object asset : doc["assets"]) {
                if (std::string_view url_sv = asset["browser_download_url"]; isPlatformReleaseAsset(url_sv)) {
                    {
                        std::lock_guard lock(updateChecker.mutex);
                        updateChecker.latestVersion = "v" + latestVersionStr;
                        updateChecker.updateUrl = std::string(url_sv);
                        updateChecker.updateCheckCompleted = true;
                        updateChecker.isUpdateAvailable = true;
                    }
                    Logger::log(NK_INFO, ("Platform update URL: " + std::string(url_sv)).c_str());
                    if (wxTheApp) {
                        wxTheApp->CallAfter([&updateChecker] { updateChecker.renderUpdatePopup(); });
                    }
                    return;
                }
            }
            Logger::log(NK_ERROR, "No update asset for this platform was found in the latest GitHub release.");
        }
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, e.what());
    }
    {
        std::lock_guard lock(updateChecker.mutex);
        updateChecker.updateCheckCompleted = true;
    }
}

void UpdateChecker::renderUpdatePopup() {
    if (updateCheckCompleted && isUpdateAvailable) {
#ifdef __APPLE__
        openUpdateDialog(
            "A new version is available: " + latestVersion + ". Would you like to download the macOS update?");
#else
        openUpdateDialog("A new version is available: " + latestVersion + ". Would you like to update?");
#endif
    }
}

void splitUrl(const std::string& url, std::string& domain, std::string& path) {
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
#ifdef _WIN32
    Logger::log(NK_INFO, "Preparing update...");

    if (updateUrl.empty()) {
        Logger::log(NK_ERROR, "NavKit: No update URL available.");
        return;
    }

    const std::filesystem::path install_dir = std::filesystem::u8path(FileUtil::getExecutablePath()).parent_path();
    const std::filesystem::path original_updater_path = install_dir / "updater.exe";
    const std::filesystem::path original_settings_path = install_dir / "NavKit.ini";

    if (!std::filesystem::exists(original_updater_path)) {
        Logger::log(NK_ERROR, ("NavKit: updater.exe not found at " + original_updater_path.string()).c_str());
        return;
    }

    const std::filesystem::path temp_path =
        std::filesystem::u8path(wxStandardPaths::Get().GetTempDir().ToUTF8().data());
    const std::filesystem::path temp_updater_dir = temp_path / ("NavKitUpdate_" + std::to_string(wxGetProcessId()));
    std::filesystem::create_directories(temp_updater_dir);
    const std::filesystem::path temp_updater_path = temp_updater_dir / "updater.exe";

    try {
        std::filesystem::copy_file(
            original_updater_path, temp_updater_path, std::filesystem::copy_options::overwrite_existing);
        Logger::log(NK_INFO, ("Copied updater to " + temp_updater_path.string()).c_str());
    } catch (const std::filesystem::filesystem_error& e) {
        Logger::log(NK_ERROR, ("NavKit: Failed to copy updater to temp directory: " + std::string(e.what())).c_str());
        return;
    }

    if (std::filesystem::exists(original_settings_path)) {
        const std::filesystem::path temp_settings_path = temp_updater_dir / "NavKit.ini";
        try {
            std::filesystem::copy_file(
                original_settings_path, temp_settings_path, std::filesystem::copy_options::overwrite_existing);
            Logger::log(
                NK_INFO, ("Copied NavKit.ini to " + temp_settings_path.string() + " to preserve settings.").c_str());
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::log(NK_ERROR,
                ("NavKit: Failed to copy NavKit.ini to temp directory, settings will not be preserved: " +
                    std::string(e.what()))
                    .c_str());
        }
    }

    const std::filesystem::path local_msi_path = temp_path / "NavKit.msi";
    std::string domain, msi_path_part;
    splitUrl(updateUrl, domain, msi_path_part);
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
        Logger::log(NK_ERROR,
            ("Error: HTTP error occurred: " + (res ? std::to_string(res->status) : "Connection failure")).c_str());
        return;
    }

    const std::vector<wxString> arguments = {pathToWxString(temp_updater_path), pathToWxString(local_msi_path),
        std::to_string(wxGetProcessId()), wxString::FromUTF8(latestVersion.c_str()), pathToWxString(install_dir)};
    Logger::log(NK_INFO, ("Launching updater: " + temp_updater_path.string()).c_str());
    wxExecuteEnv environment;
    environment.cwd = pathToWxString(install_dir);
    if (executeArguments(arguments, wxEXEC_ASYNC, &environment) == 0) {
        Logger::log(NK_ERROR, "NavKit: Failed to launch updater.exe from temp directory.");
        return;
    }
    Logger::log(NK_INFO, "Closing NavKit to allow update to proceed.");

    wxMilliSleep(1000);
    Logger::getInstance().stop();
    exit(0);
#elif defined(__APPLE__)
    if (updateUrl.empty()) {
        Logger::log(NK_ERROR, "NavKit: No macOS update URL available.");
        return;
    }
    if (!wxLaunchDefaultBrowser(wxString::FromUTF8(updateUrl))) {
        Logger::log(NK_ERROR, "NavKit: Could not open the macOS update download URL.");
    }
#else
    Logger::log(NK_WARN, "Automatic updates are only supported on Windows.");
#endif
}

void UpdateChecker::openUpdateDialog(const std::string& message) {
    if (wxMessageBox(wxString::FromUTF8(message), "Update available", wxYES_NO | wxICON_INFORMATION, getMainFrame()) ==
        wxYES) {
        performUpdate();
    }
}

bool UpdateChecker::isVersionGreaterThan(const std::string& v1, const std::string& v2) {
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
        if (n1 > n2) {
            return true;
        }
        if (n1 < n2) {
            return false;
        }
    }
    return false;
}
