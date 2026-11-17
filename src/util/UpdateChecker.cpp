#include "../../include/NavKit/util/UpdateChecker.h"
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <filesystem>
#include <fstream>
#include <httplib.h>
#include <memory>
#include <openssl/evp.h>
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
        return url.ends_with("NavKit-macos-universal.dmg");
#elif defined(__linux__)
        return url.ends_with("NavKit-linux-x86_64.AppImage");
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

    class VerifiedDownload {
    public:
        explicit VerifiedDownload(const std::filesystem::path& path) :
            output(path, std::ios::binary | std::ios::trunc), hashContext(EVP_MD_CTX_new(), EVP_MD_CTX_free) {
            hashing = hashContext && EVP_DigestInit_ex(hashContext.get(), EVP_sha256(), nullptr) == 1;
        }

        bool isOpen() const {
            return output.is_open() && hashing;
        }

        void close() {
            if (output.is_open()) {
                output.close();
            }
        }

        bool write(const char* data, const size_t length) {
            output.write(data, static_cast<std::streamsize>(length));
            hashing = hashing && EVP_DigestUpdate(hashContext.get(), data, length) == 1;
            bytesWritten += length;
            return output.good() && hashing;
        }

        std::string verify(const uint64_t expectedSize, const std::string& expectedSha256) {
            output.close();
            if (!output) {
                return "could not write the update file";
            }
            if (bytesWritten != expectedSize) {
                return "expected " + std::to_string(expectedSize) + " bytes but received " +
                    std::to_string(bytesWritten);
            }
            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int digestLength = 0;
            if (!hashing || EVP_DigestFinal_ex(hashContext.get(), digest, &digestLength) != 1) {
                return "could not hash the update file";
            }
            std::string actualSha256;
            for (unsigned int i = 0; i < digestLength; ++i) {
                constexpr char hexDigits[] = "0123456789abcdef";
                actualSha256 += hexDigits[digest[i] >> 4];
                actualSha256 += hexDigits[digest[i] & 0xF];
            }
            if (actualSha256 != expectedSha256) {
                return "SHA-256 mismatch: expected " + expectedSha256 + ", got " + actualSha256;
            }
            return {};
        }

    private:
        std::ofstream output;
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hashContext;
        bool hashing = false;
        uint64_t bytesWritten = 0;
    };

    std::string downloadVerified(httplib::Client& client, const std::string& downloadPath,
        const std::filesystem::path& destination, const uint64_t expectedSize, const std::string& expectedSha256) {
        VerifiedDownload download(destination);
        if (!download.isOpen()) {
            return "could not create " + destination.string();
        }
        const auto response = client.Get(
            downloadPath, [&](const char* data, const size_t dataLength) { return download.write(data, dataLength); });
        std::string failure;
        if (!response) {
            failure = "connection failure";
        } else if (response->status != 200) {
            failure = "HTTP " + std::to_string(response->status);
        } else {
            failure = download.verify(expectedSize, expectedSha256);
        }
        if (!failure.empty()) {
            download.close();
            std::error_code error;
            std::filesystem::remove(destination, error);
        }
        return failure;
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

    std::string responseBody;
    bool requestSuccess = false;

    {
        httplib::Client cli("https://api.github.com");
        try {
            auto res = cli.Get("/repos/glacier-modding/NavKit/releases/latest");
            if (res == nullptr) {
                Logger::log(NK_ERROR, "GitHub API request failed");
                {
                    std::lock_guard lock(updateChecker.mutex);
                    updateChecker.updateCheckCompleted = true;
                }
                return;
            }
            if (res->status != 200) {
                Logger::log(NK_ERROR, "GitHub API request failed with status: %s", std::to_string(res->status).c_str());
                {
                    std::lock_guard lock(updateChecker.mutex);
                    updateChecker.updateCheckCompleted = true;
                }
                return;
            }
            responseBody = res->body;
            requestSuccess = true;
        } catch (const std::exception& e) {
            Logger::log(NK_ERROR, e.what());
            {
                std::lock_guard lock(updateChecker.mutex);
                updateChecker.updateCheckCompleted = true;
            }
            return;
        }
    } // httplib::Client destroyed here

    if (!requestSuccess) {
        {
            std::lock_guard lock(updateChecker.mutex);
            updateChecker.updateCheckCompleted = true;
        }
        return;
    }

    try {
        updateChecker.responseBody = responseBody;
        simdjson::dom::parser parser;
        simdjson::dom::element doc = parser.parse(updateChecker.responseBody);

        std::string latestVersionStr;
        std::string platformUpdateUrl;
        uint64_t platformUpdateSize = 0;
        std::string platformUpdateSha256;
        bool foundPlatformAsset = false;

        std::string_view latestVersionSV = doc["tag_name"];
        if (latestVersionSV.empty()) {
            Logger::log(NK_ERROR, "Error checking for updates.");
        } else {
            latestVersionStr = std::string(latestVersionSV.substr(1));

            const std::string currentVersionStr = std::string(NavKit_VERSION_MAJOR) + "." +
                std::string(NavKit_VERSION_MINOR) + "." + std::string(NavKit_VERSION_PATCH);
            Logger::log(NK_INFO, ("Current NavKit version: " + currentVersionStr).c_str());
            Logger::log(NK_INFO, ("Latest NavKit version: " + latestVersionStr).c_str());
            const bool updateAvailable = isVersionGreaterThan(latestVersionStr, currentVersionStr);
            Logger::log(NK_INFO, std::string(updateAvailable ? "Update available." : "No update available.").c_str());
            if (updateAvailable) {
                simdjson::dom::array assets = doc["assets"];
                for (simdjson::dom::element asset : assets) {
                    std::string_view url_sv = asset["browser_download_url"];
                    if (isPlatformReleaseAsset(url_sv)) {
                        constexpr std::string_view sha256Prefix = "sha256:";
                        std::string_view digest;
                        if (asset["size"].get(platformUpdateSize) || asset["digest"].get(digest) ||
                            !digest.starts_with(sha256Prefix)) {
                            Logger::log(NK_ERROR, "The update asset has no size or SHA-256 digest to verify against.");
                            break;
                        }
                        platformUpdateSha256 = std::string(digest.substr(sha256Prefix.size()));
                        platformUpdateUrl = std::string(url_sv);
                        foundPlatformAsset = true;
                        Logger::log(NK_INFO, ("Platform update URL: " + platformUpdateUrl).c_str());
                        break;
                    }
                }
                if (!foundPlatformAsset) {
                    Logger::log(NK_ERROR, "No update asset for this platform was found in the latest GitHub release.");
                }
            }
        }

        if (foundPlatformAsset && !latestVersionStr.empty()) {
            {
                std::lock_guard lock(updateChecker.mutex);
                updateChecker.latestVersion = "v" + latestVersionStr;
                updateChecker.updateUrl = platformUpdateUrl;
                updateChecker.updateSize = platformUpdateSize;
                updateChecker.updateSha256 = platformUpdateSha256;
                updateChecker.updateCheckCompleted = true;
                updateChecker.isUpdateAvailable = true;
            }
            if (wxTheApp) {
                wxTheApp->CallAfter([&updateChecker] { updateChecker.renderUpdatePopup(); });
            }
            return;
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
    std::string localLatestVersion;
    bool localUpdateCheckCompleted;
    bool localIsUpdateAvailable;
    {
        std::lock_guard lock(mutex);
        localLatestVersion = latestVersion;
        localUpdateCheckCompleted = updateCheckCompleted;
        localIsUpdateAvailable = isUpdateAvailable;
    }

    if (localUpdateCheckCompleted && localIsUpdateAvailable) {
#ifdef __APPLE__
        openUpdateDialog("A new version is available: " + localLatestVersion +
            ". Would you like to download and install the macOS update?");
#else
        openUpdateDialog("A new version is available: " + localLatestVersion + ". Would you like to update?");
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

void UpdateChecker::performUpdate() {
#ifdef _WIN32
    Logger::log(NK_INFO, "Preparing update...");

    std::string localUpdateUrl;
    std::string localLatestVersion;
    uint64_t localUpdateSize;
    std::string localUpdateSha256;
    {
        std::lock_guard lock(mutex);
        localUpdateUrl = updateUrl;
        localLatestVersion = latestVersion;
        localUpdateSize = updateSize;
        localUpdateSha256 = updateSha256;
    }

    if (localUpdateUrl.empty()) {
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
    splitUrl(localUpdateUrl, domain, msi_path_part);
    httplib::Client cli("https://" + domain);
    cli.set_follow_location(true);

    Logger::log(NK_INFO, ("Downloading file: https://" + domain + msi_path_part).c_str());
    Logger::log(NK_INFO, ("Local path: " + local_msi_path.string()).c_str());

    if (const std::string failure =
            downloadVerified(cli, msi_path_part, local_msi_path, localUpdateSize, localUpdateSha256);
        !failure.empty()) {
        Logger::log(NK_ERROR, ("NavKit: Update download failed: " + failure).c_str());
        return;
    }
    Logger::log(NK_INFO, ("File downloaded and verified at " + local_msi_path.string()).c_str());

    const std::vector<wxString> arguments = {pathToWxString(temp_updater_path), pathToWxString(local_msi_path),
        std::to_string(wxGetProcessId()), wxString::FromUTF8(localLatestVersion.c_str()), pathToWxString(install_dir)};
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
    std::string localUpdateUrl;
    std::string localLatestVersion;
    uint64_t localUpdateSize;
    std::string localUpdateSha256;
    {
        std::lock_guard lock(mutex);
        localUpdateUrl = updateUrl;
        localLatestVersion = latestVersion;
        localUpdateSize = updateSize;
        localUpdateSha256 = updateSha256;
    }

    if (localUpdateUrl.empty()) {
        Logger::log(NK_ERROR, "NavKit: No macOS update URL available.");
        return;
    }

    const std::filesystem::path executablePath = std::filesystem::u8path(FileUtil::getExecutablePath());
    const std::filesystem::path appBundlePath = executablePath.parent_path().parent_path().parent_path();
    const std::filesystem::path updaterPath = executablePath.parent_path() / "NavKitUpdater";
    if (!std::filesystem::exists(updaterPath)) {
        Logger::log(NK_ERROR, ("NavKit: macOS updater not found at " + updaterPath.string()).c_str());
        return;
    }

    const std::filesystem::path tempPath = std::filesystem::u8path(wxStandardPaths::Get().GetTempDir().ToUTF8().data());
    const std::string updateId = std::to_string(wxGetProcessId());
    const std::filesystem::path dmgPath = tempPath / ("NavKitUpdate_" + updateId + ".dmg");

    std::string domain, downloadPath;
    splitUrl(localUpdateUrl, domain, downloadPath);
    httplib::Client cli("https://" + domain);
    cli.set_follow_location(true);
    Logger::log(NK_INFO, ("Downloading macOS update " + localLatestVersion + ".").c_str());
    if (const std::string failure = downloadVerified(cli, downloadPath, dmgPath, localUpdateSize, localUpdateSha256);
        !failure.empty()) {
        Logger::log(NK_ERROR, ("NavKit: macOS update download failed: " + failure).c_str());
        return;
    }

    const std::vector<wxString> arguments = {pathToWxString(updaterPath), pathToWxString(dmgPath),
        std::to_string(wxGetProcessId()), pathToWxString(appBundlePath)};
    Logger::log(NK_INFO, "Launching macOS updater.");
    if (executeArguments(arguments, wxEXEC_ASYNC, nullptr) == 0) {
        std::error_code error;
        std::filesystem::remove(dmgPath, error);
        Logger::log(NK_ERROR, "NavKit: Failed to launch the macOS updater.");
        return;
    }

    Logger::log(NK_INFO, "Closing NavKit to allow the macOS update to proceed.");
    wxMilliSleep(1000);
    Logger::getInstance().stop();
    exit(0);
#elif defined(__linux__)
    std::string localUpdateUrl;
    std::string localLatestVersion;
    uint64_t localUpdateSize;
    std::string localUpdateSha256;
    {
        std::lock_guard lock(mutex);
        localUpdateUrl = updateUrl;
        localLatestVersion = latestVersion;
        localUpdateSize = updateSize;
        localUpdateSha256 = updateSha256;
    }

    if (localUpdateUrl.empty()) {
        Logger::log(NK_ERROR, "NavKit: No Linux update URL available.");
        return;
    }

    const char* appImageEnvironment = std::getenv("APPIMAGE");
    if (appImageEnvironment == nullptr || *appImageEnvironment == '\0') {
        Logger::log(NK_WARN, "Automatic updates are only supported when running the NavKit AppImage.");
        wxLaunchDefaultBrowser("https://github.com/glacier-modding/NavKit/releases/latest");
        return;
    }

    // Renaming within the AppImage's own directory keeps the swap atomic and on one filesystem
    const std::filesystem::path appImagePath = std::filesystem::u8path(appImageEnvironment);
    const std::filesystem::path stagedAppImagePath = appImagePath.parent_path() /
        ("." + appImagePath.filename().string() + ".update-" + std::to_string(wxGetProcessId()));

    std::string domain, downloadPath;
    splitUrl(localUpdateUrl, domain, downloadPath);
    httplib::Client cli("https://" + domain);
    cli.set_follow_location(true);
    Logger::log(NK_INFO, ("Downloading Linux update " + localLatestVersion + ".").c_str());
    if (const std::string failure =
            downloadVerified(cli, downloadPath, stagedAppImagePath, localUpdateSize, localUpdateSha256);
        !failure.empty()) {
        Logger::log(NK_ERROR, ("NavKit: Linux update download failed: " + failure).c_str());
        return;
    }

    std::error_code error;
    using std::filesystem::perms;
    std::filesystem::permissions(stagedAppImagePath,
        perms::owner_all | perms::group_read | perms::group_exec | perms::others_read | perms::others_exec, error);
    if (!error) {
        std::filesystem::rename(stagedAppImagePath, appImagePath, error);
    }
    if (error) {
        const std::string replaceError = error.message();
        std::filesystem::remove(stagedAppImagePath, error);
        Logger::log(NK_ERROR, ("NavKit: Could not replace " + appImagePath.string() + ": " + replaceError).c_str());
        return;
    }

    Logger::log(NK_INFO, "Relaunching updated NavKit AppImage.");
    if (executeArguments({pathToWxString(appImagePath)}, wxEXEC_ASYNC, nullptr) == 0) {
        Logger::log(NK_ERROR, "NavKit: Update installed but the new AppImage could not be launched.");
        return;
    }
    wxMilliSleep(1000);
    Logger::getInstance().stop();
    exit(0);
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
