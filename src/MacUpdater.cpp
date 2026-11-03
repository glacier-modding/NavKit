#include <wx/init.h>
#include <wx/utils.h>

#include "../include/NavKit/util/UpdaterUtils.h"

#include <cerrno>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <signal.h>
#include <string>
#include <thread>
#include <vector>

namespace {
    wxString pathToWxString(const std::filesystem::path& path) {
        const std::u8string utf8Path = path.u8string();
        return wxString::FromUTF8(reinterpret_cast<const char*>(utf8Path.c_str()));
    }

    int executeArguments(const std::vector<wxString>& arguments) {
        std::vector<const wxChar*> argv;
        argv.reserve(arguments.size() + 1);
        for (const wxString& argument : arguments) {
            argv.push_back(argument.c_str());
        }
        argv.push_back(nullptr);
        return wxExecute(argv.data(), wxEXEC_SYNC);
    }

    int showUpdateError() {
        return executeArguments({"/usr/bin/osascript", "-e",
            "display alert \"NavKit update failed\" message \"NavKit could not finish updating. Reopen it from the "
            "Applications folder.\""});
    }

    bool waitForProcessToExit(const pid_t processId) {
        while (true) {
            errno = 0;
            if (kill(processId, 0) != 0) {
                if (errno == ESRCH) {
                    return true;
                }
                if (errno != EPERM) {
                    return false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
} // namespace

int main(const int argc, char** argv) {
    wxInitializer wxInitializer;
    if (!wxInitializer.IsOk() || argc != 4) {
        return 1;
    }

    long processId = 0;
    const char* pidEnd = argv[2] + std::char_traits<char>::length(argv[2]);
    if (const auto [end, error] = std::from_chars(argv[2], pidEnd, processId);
        error != std::errc{} || end != pidEnd || processId <= 0) {
        return 1;
    }

    const std::filesystem::path dmgPath(argv[1]);
    const std::filesystem::path appBundlePath(argv[3]);
    if (!NavKit::Updater::isValidMacUpdateArguments(dmgPath, processId, appBundlePath) ||
        !std::filesystem::is_regular_file(dmgPath)) {
        return 1;
    }

    if (!waitForProcessToExit(static_cast<pid_t>(processId))) {
        showUpdateError();
        return 1;
    }

    const std::filesystem::path tempPath = std::filesystem::temp_directory_path();
    const NavKit::Updater::MacUpdatePaths paths = NavKit::Updater::macUpdatePaths(tempPath, appBundlePath, processId);

    std::error_code error;
    if (std::filesystem::exists(paths.mount) || std::filesystem::exists(paths.stagedBundle) ||
        std::filesystem::exists(paths.backupBundle)) {
        showUpdateError();
        return 1;
    }
    if (!std::filesystem::create_directory(paths.mount, error) || error) {
        showUpdateError();
        return 1;
    }

    const int mountStatus = executeArguments({"hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint",
        pathToWxString(paths.mount), pathToWxString(dmgPath)});
    if (mountStatus != 0 || !std::filesystem::is_directory(paths.sourceBundle)) {
        executeArguments({"hdiutil", "detach", pathToWxString(paths.mount)});
        std::filesystem::remove(paths.mount, error);
        showUpdateError();
        return 1;
    }

    const int copyStatus =
        executeArguments({"ditto", pathToWxString(paths.sourceBundle), pathToWxString(paths.stagedBundle)});
    const int detachStatus = executeArguments({"hdiutil", "detach", pathToWxString(paths.mount)});
    std::filesystem::remove(paths.mount, error);
    if (copyStatus != 0 || detachStatus != 0) {
        std::filesystem::remove_all(paths.stagedBundle, error);
        showUpdateError();
        return 1;
    }

    if (!NavKit::Updater::replaceMacAppBundle(paths.stagedBundle, appBundlePath, paths.backupBundle) ||
        executeArguments({"open", "-n", pathToWxString(appBundlePath)}) != 0) {
        showUpdateError();
        return 1;
    }
    std::filesystem::remove(dmgPath, error);
    return 0;
}
