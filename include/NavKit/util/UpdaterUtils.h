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

    inline std::vector<std::string> windowsInstallerArguments(const std::filesystem::path& msiPath,
        const std::filesystem::path& logPath, const std::filesystem::path& installDirectory) {
        auto normalizedMsi = msiPath;
        auto normalizedLog = logPath;
        auto normalizedDir = installDirectory;
        normalizedMsi.make_preferred();
        normalizedLog.make_preferred();
        normalizedDir.make_preferred();

        std::string dirStr = pathToUtf8(normalizedDir);
        while (dirStr.size() > 3 && (dirStr.back() == '\\' || dirStr.back() == '/')) {
            dirStr.pop_back();
        }

        return {"msiexec.exe", "/i", pathToUtf8(normalizedMsi),
            "/passive", // or "/qn" for completely silent UI
            "/norestart", "/L*v", pathToUtf8(normalizedLog), "MSIRESTARTMANAGERCONTROL=Disable",
            "INSTALLFOLDER=" + dirStr};
    }

    // msiexec parses its own command line and only understands property assignments written as
    // NAME="value with spaces". Generic argv quoting (as done by wxExecute(argv) / CreateProcess helpers)
    // produces "NAME=value with spaces", which msiexec rejects by showing its usage dialog.
    inline std::string quoteWindowsArgument(const std::string& argument) {
        if (!argument.empty() && argument.find_first_of(" \t\"") == std::string::npos) {
            return argument;
        }
        return "\"" + argument + "\"";
    }

    inline bool isMsiPropertyAssignment(const std::string& argument, std::string::size_type& equalsPos) {
        equalsPos = argument.find('=');
        if (equalsPos == std::string::npos || equalsPos == 0) {
            return false;
        }
        for (std::string::size_type i = 0; i < equalsPos; ++i) {
            const char c = argument[i];
            const bool valid =
                (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
            if (!valid) {
                return false;
            }
        }
        return true;
    }

    inline std::string windowsInstallerCommandLine(const std::vector<std::string>& arguments) {
        std::string commandLine;
        for (const std::string& argument : arguments) {
            if (!commandLine.empty()) {
                commandLine += ' ';
            }
            std::string::size_type equalsPos = 0;
            if (isMsiPropertyAssignment(argument, equalsPos)) {
                commandLine += argument.substr(0, equalsPos + 1) + quoteWindowsArgument(argument.substr(equalsPos + 1));
            } else {
                commandLine += quoteWindowsArgument(argument);
            }
        }
        return commandLine;
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
