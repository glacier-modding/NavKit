#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace NavKit::Updater {
    inline std::string pathToUtf8(const std::filesystem::path& path) {
        const std::u8string utf8Path = path.u8string();
        return {reinterpret_cast<const char*>(utf8Path.data()), utf8Path.size()};
    }

    inline std::vector<std::string> windowsInstallerArguments(
        const std::filesystem::path& msiPath, const std::filesystem::path& logPath) {
        return {"msiexec.exe", "/i", pathToUtf8(msiPath), "/passive", "/norestart", "/L*v", pathToUtf8(logPath),
            "MSIRESTARTMANAGERCONTROL=Disable"};
    }

    inline std::filesystem::path windowsUpdateLogPath(
        const std::filesystem::path& installDirectory, const std::string& version) {
        return installDirectory / ("NavKit_Update_MSI_Log_" + version + ".txt");
    }

    inline std::filesystem::path windowsSettingsSource(const std::filesystem::path& updaterPath) {
        return updaterPath.parent_path() / "NavKit.ini";
    }

    inline std::filesystem::path windowsSettingsDestination(const std::filesystem::path& installDirectory) {
        return installDirectory / "NavKit.ini.old";
    }

    inline bool windowsInstallerSucceeded(const long exitCode) {
        return exitCode == 0;
    }

    struct MacUpdatePaths {
        std::filesystem::path mount;
        std::filesystem::path sourceBundle;
        std::filesystem::path stagedBundle;
        std::filesystem::path backupBundle;
    };

    inline bool isValidMacUpdateArguments(
        const std::filesystem::path& dmgPath, const long processId, const std::filesystem::path& appBundlePath) {
        return processId > 0 && dmgPath.is_absolute() && dmgPath.extension() == ".dmg" && appBundlePath.is_absolute() &&
            appBundlePath.filename() == "NavKit.app";
    }

    inline MacUpdatePaths macUpdatePaths(
        const std::filesystem::path& tempPath, const std::filesystem::path& appBundlePath, const long processId) {
        const std::string updateId = std::to_string(processId);
        const std::filesystem::path mountPath = tempPath / ("NavKitUpdateMount_" + updateId);
        return {
            .mount = mountPath,
            .sourceBundle = mountPath / "NavKit.app",
            .stagedBundle = appBundlePath.parent_path() / (".NavKit.app.update." + updateId),
            .backupBundle = appBundlePath.parent_path() / (".NavKit.app.backup." + updateId),
        };
    }

    inline bool replaceMacAppBundle(const std::filesystem::path& stagedBundlePath,
        const std::filesystem::path& appBundlePath, const std::filesystem::path& backupBundlePath) {
        std::error_code error;
        std::filesystem::rename(appBundlePath, backupBundlePath, error);
        if (error) {
            return false;
        }

        std::filesystem::rename(stagedBundlePath, appBundlePath, error);
        if (error) {
            std::error_code restoreError;
            std::filesystem::rename(backupBundlePath, appBundlePath, restoreError);
            std::filesystem::remove_all(stagedBundlePath, restoreError);
            return false;
        }

        std::filesystem::remove_all(backupBundlePath, error);
        return !error;
    }
} // namespace NavKit::Updater
