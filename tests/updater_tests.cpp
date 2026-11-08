#include "../include/NavKit/util/UpdaterUtils.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
    void require(const bool condition, const std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    class TemporaryDirectory {
    public:
        TemporaryDirectory() {
            const auto uniqueId = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() / ("navkit-updater-tests-" + std::to_string(uniqueId));
            std::filesystem::create_directories(path);
        }

        ~TemporaryDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        std::filesystem::path path;
    };

    void writeFile(const std::filesystem::path& path, const std::string_view contents) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file << contents;
        require(file.good(), "could not create test fixture");
    }

    std::string readFile(const std::filesystem::path& path) {
        std::ifstream file(path);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void testWindowsInstallerArguments() {
        const std::filesystem::path msiPath =
            std::filesystem::temp_directory_path() / "NavKit Downloads" / "NavKit.msi";
        const std::filesystem::path logPath =
            std::filesystem::temp_directory_path() / "NavKit Install" / "NavKit_Update_MSI_Log_v3.19.1.txt";
        const std::filesystem::path installDirectory =
            std::filesystem::temp_directory_path() / "Custom Install" / "NavKit";
        const auto arguments = NavKit::Updater::windowsInstallerArguments(msiPath, logPath, installDirectory);
        const std::vector<std::string> expected = {"msiexec.exe", "/i", NavKit::Updater::pathToUtf8(msiPath),
            "/passive", "/norestart", "/L*v", NavKit::Updater::pathToUtf8(logPath), "MSIRESTARTMANAGERCONTROL=Disable",
            "INSTALLFOLDER=" + NavKit::Updater::pathToUtf8(installDirectory)};
        require(arguments == expected, "Windows MSI arguments changed or paths with spaces were split");
    }

    void testWindowsUpdatePathsAndResult() {
        const std::filesystem::path installDirectory =
            std::filesystem::temp_directory_path() / "Program Files" / "NavKit";
        const std::filesystem::path updaterPath =
            std::filesystem::temp_directory_path() / "NavKitUpdate" / "updater.exe";
        require(NavKit::Updater::windowsUpdateLogPath(installDirectory, "v3.19.1") ==
                installDirectory / "NavKit_Update_MSI_Log_v3.19.1.txt",
            "Windows MSI log path is incorrect");
        require(NavKit::Updater::windowsSettingsSource(updaterPath) == updaterPath.parent_path() / "NavKit.ini",
            "Windows updater should read staged settings beside itself");
        require(NavKit::Updater::windowsSettingsDestination(installDirectory) == installDirectory / "NavKit.ini.old",
            "Windows updater settings merge destination is incorrect");
        require(NavKit::Updater::windowsInstallerSucceeded(0), "zero MSI exit code should indicate success");
        require(!NavKit::Updater::windowsInstallerSucceeded(1603), "MSI failure exit code was treated as success");
        require(!NavKit::Updater::windowsInstallerSucceeded(-1), "installer launch failure was treated as success");
    }

    void testMacUpdateArgumentValidationAndPaths() {
        TemporaryDirectory temporaryDirectory;
        const std::filesystem::path dmgPath = temporaryDirectory.path / "NavKitUpdate.dmg";
        const std::filesystem::path appBundle = temporaryDirectory.path / "NavKit.app";
        const std::filesystem::path tempPath = temporaryDirectory.path / "temporary";
        require(NavKit::Updater::isValidMacUpdateArguments(dmgPath, 123, appBundle),
            "valid macOS updater arguments were rejected");
        require(!NavKit::Updater::isValidMacUpdateArguments("NavKitUpdate.dmg", 123, appBundle),
            "relative macOS disk image path was accepted");
        require(
            !NavKit::Updater::isValidMacUpdateArguments(temporaryDirectory.path / "NavKitUpdate.zip", 123, appBundle),
            "non-DMG update path was accepted");
        require(!NavKit::Updater::isValidMacUpdateArguments(dmgPath, 0, appBundle),
            "invalid parent process ID was accepted");
        require(!NavKit::Updater::isValidMacUpdateArguments(dmgPath, 123, temporaryDirectory.path / "Other.app"),
            "unexpected app bundle was accepted");
        require(!NavKit::Updater::isValidMacUpdateArguments(dmgPath, 123, "NavKit.app"),
            "relative app bundle path was accepted");

        const NavKit::Updater::MacUpdatePaths paths = NavKit::Updater::macUpdatePaths(tempPath, appBundle, 123);
        require(paths.mount == tempPath / "NavKitUpdateMount_123", "macOS update mount path is incorrect");
        require(paths.sourceBundle == paths.mount / "NavKit.app", "macOS update source bundle path is incorrect");
        require(paths.stagedBundle == temporaryDirectory.path / ".NavKit.app.update.123",
            "macOS update staging path is incorrect");
        require(paths.backupBundle == temporaryDirectory.path / ".NavKit.app.backup.123",
            "macOS update rollback path is incorrect");
    }

    void testMacBundleReplacementAndRollback() {
        TemporaryDirectory temporaryDirectory;
        const std::filesystem::path appBundle = temporaryDirectory.path / "NavKit.app";
        const std::filesystem::path stagedBundle = temporaryDirectory.path / ".NavKit.app.update";
        const std::filesystem::path backupBundle = temporaryDirectory.path / ".NavKit.app.backup";
        const std::filesystem::path appMarker = appBundle / "version.txt";
        const std::filesystem::path stagedMarker = stagedBundle / "version.txt";

        writeFile(appMarker, "old");
        writeFile(stagedMarker, "new");
        require(NavKit::Updater::replaceMacAppBundle(stagedBundle, appBundle, backupBundle),
            "macOS update should replace the app bundle");
        require(readFile(appBundle / "version.txt") == "new", "updated app bundle was not installed");
        require(!std::filesystem::exists(backupBundle), "successful update left the backup bundle behind");

        std::filesystem::remove_all(appBundle);
        std::filesystem::remove_all(stagedBundle);
        writeFile(appMarker, "old");
        require(!NavKit::Updater::replaceMacAppBundle(stagedBundle, appBundle, backupBundle),
            "macOS update with a missing staged bundle should fail");
        require(readFile(appMarker) == "old", "failed update did not restore the previous app bundle");
        require(!std::filesystem::exists(backupBundle), "failed update left the backup in place");
    }
} // namespace

int main() {
    try {
        testWindowsInstallerArguments();
        testWindowsUpdatePathsAndResult();
        testMacUpdateArgumentValidationAndPaths();
        testMacBundleReplacementAndRollback();
        std::cout << "[pass] Windows and macOS updater regression tests\n";
    } catch (const std::exception& error) {
        std::cerr << "[fail] " << error.what() << '\n';
        return 1;
    }
    return 0;
}
