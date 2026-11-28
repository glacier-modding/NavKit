#include "../../include/NavKit/bulk/LivePipeline.h"

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/GameConnection.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/ProcessFlow.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/GridGenerator.h"
#include "../../include/navkit-rpkg-lib/navkit-rpkg-lib.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <ranges>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <tlhelp32.h>
#include <windows.h>
#endif
#include <wx/utils.h>
#include <SimpleIni.h>

namespace NavKit::LivePipeline {
    namespace {
        std::mutex logMutex;
        LogSink logSink;
        std::filesystem::path resourceDirectory;

        /// Collects one message with << and passes it to the log sink when it goes out of scope.
        class LogStream {
        public:
            explicit LogStream(const LogLevel level) : level(level) {}
            LogStream(const LogStream&) = delete;
            LogStream& operator=(const LogStream&) = delete;

            ~LogStream() {
                std::string message = stream.str();
                while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
                    message.pop_back();
                }
                if (!message.empty()) {
                    log(level, message);
                }
            }

            template <typename T> LogStream& operator<<(const T& value) {
                stream << value;
                return *this;
            }

            LogStream& operator<<(std::ostream& (*)(std::ostream&)) {
                return *this;
            }

        private:
            LogLevel level;
            std::ostringstream stream;
        };
        wxString pathToWxString(const std::filesystem::path& path) {
            return wxString(path.wstring());
        }

#ifdef _WIN32
        constexpr SHORT pipelineConsoleWidth = 240;

        std::wstring normalizedPath(const std::filesystem::path& path) {
            std::error_code error;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
            if (error) {
                normalized = std::filesystem::absolute(path, error);
            }
            require(!error, "Could not resolve the HITMAN executable path.");
            return normalized.lexically_normal().wstring();
        }

        std::vector<DWORD> findHitmanProcesses(const std::filesystem::path& hitmanExecutable) {
            const std::wstring expectedPath = normalizedPath(hitmanExecutable);
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            require(snapshot != INVALID_HANDLE_VALUE, "Could not enumerate running processes to close HITMAN.");

            std::vector<DWORD> processIds;
            PROCESSENTRY32W processEntry{};
            processEntry.dwSize = sizeof(processEntry);
            if (Process32FirstW(snapshot, &processEntry)) {
                do {
                    const HANDLE process =
                        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processEntry.th32ProcessID);
                    if (process == nullptr) {
                        continue;
                    }
                    std::vector<wchar_t> executablePath(32768);
                    DWORD pathLength = static_cast<DWORD>(executablePath.size());
                    const BOOL pathFound = QueryFullProcessImageNameW(process, 0, executablePath.data(), &pathLength);
                    CloseHandle(process);
                    if (pathFound &&
                        CompareStringOrdinal(expectedPath.c_str(), -1, executablePath.data(),
                            static_cast<int>(pathLength), TRUE) == CSTR_EQUAL) {
                        processIds.push_back(processEntry.th32ProcessID);
                    }
                } while (Process32NextW(snapshot, &processEntry));
            }
            CloseHandle(snapshot);
            return processIds;
        }

        struct CloseWindowContext {
            DWORD processId;
            bool closeRequested = false;
        };

        BOOL CALLBACK requestWindowClose(const HWND window, const LPARAM parameter) {
            auto& context = *reinterpret_cast<CloseWindowContext*>(parameter);
            DWORD windowProcessId = 0;
            GetWindowThreadProcessId(window, &windowProcessId);
            if (windowProcessId == context.processId && IsWindowVisible(window)) {
                context.closeRequested = PostMessageW(window, WM_CLOSE, 0, 0) != FALSE || context.closeRequested;
            }
            return TRUE;
        }
#endif

        std::string trim(std::string value) {
            const auto isNotSpace = [](const unsigned char character) { return !std::isspace(character); };
            const auto first = std::ranges::find_if(value, isNotSpace);
            const auto last = std::ranges::find_if(value | std::views::reverse, isNotSpace).base();
            return first < last ? std::string(first, last) : std::string{};
        }

        std::vector<std::string> updateAutoLoadScene(
            const std::filesystem::path& modsIni, const std::vector<std::string>& replacementSettings) {
            require(std::filesystem::is_regular_file(modsIni), "Could not find Retail\\mods.ini.");
            std::ifstream input(modsIni);
            require(input.is_open(), "Could not read Retail\\mods.ini.");
            std::vector<std::string> lines;
            for (std::string line; std::getline(input, line);) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(std::move(line));
            }
            require(input.eof(), "Could not finish reading Retail\\mods.ini.");

            std::vector<std::string> originalSettings;
            bool inSdkSection = false;
            bool sdkSectionFound = false;
            bool settingWritten = false;
            std::vector<std::string> updatedLines;
            updatedLines.reserve(lines.size() + 2);
            for (size_t index = 0; index < lines.size(); ++index) {
                const std::string& line = lines[index];
                std::string content = trim(line);
                if (index == 0 && content.starts_with("\xef\xbb\xbf")) {
                    content.erase(0, 3);
                    content = trim(std::move(content));
                }
                if (content.size() >= 2 && content.front() == '[' && content.back() == ']') {
                    if (inSdkSection && !settingWritten) {
                        updatedLines.insert(updatedLines.end(), replacementSettings.begin(), replacementSettings.end());
                        settingWritten = true;
                    }
                    inSdkSection = trim(content.substr(1, content.size() - 2)) == "sdk";
                    sdkSectionFound = sdkSectionFound || inSdkSection;
                    updatedLines.push_back(line);
                    continue;
                }

                if (inSdkSection) {
                    const size_t equals = content.find('=');
                    if (equals != std::string::npos && trim(content.substr(0, equals)) == "auto_load_scene") {
                        originalSettings.push_back(line);
                        if (!settingWritten) {
                            updatedLines.insert(
                                updatedLines.end(), replacementSettings.begin(), replacementSettings.end());
                            settingWritten = true;
                        }
                        continue;
                    }
                }
                updatedLines.push_back(line);
            }

            if (inSdkSection && !settingWritten) {
                updatedLines.insert(updatedLines.end(), replacementSettings.begin(), replacementSettings.end());
                settingWritten = true;
            }
            if (!sdkSectionFound && !replacementSettings.empty()) {
                if (!updatedLines.empty() && !updatedLines.back().empty()) {
                    updatedLines.emplace_back();
                }
                updatedLines.emplace_back("[sdk]");
                updatedLines.insert(updatedLines.end(), replacementSettings.begin(), replacementSettings.end());
            }

            std::ofstream output(modsIni, std::ios::binary | std::ios::trunc);
            require(output.is_open(), "Could not update Retail\\mods.ini.");
            for (size_t index = 0; index < updatedLines.size(); ++index) {
                if (index != 0) {
                    output << "\r\n";
                }
                output << updatedLines[index];
            }
            output << "\r\n";
            output.flush();
            require(output.good(), "Could not save the auto_load_scene setting to Retail\\mods.ini.");
            return originalSettings;
        }

        std::filesystem::path findSteamExecutable(const std::filesystem::path& hitmanDirectory) {
#ifdef _WIN32
            HKEY steamKey = nullptr;
            if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_QUERY_VALUE, &steamKey) ==
                ERROR_SUCCESS) {
                wchar_t steamPath[MAX_PATH]{};
                DWORD valueSize = sizeof(steamPath);
                DWORD valueType = 0;
                const LONG queryResult = RegQueryValueExW(
                    steamKey, L"SteamPath", nullptr, &valueType, reinterpret_cast<BYTE*>(steamPath), &valueSize);
                RegCloseKey(steamKey);
                if (queryResult == ERROR_SUCCESS && (valueType == REG_SZ || valueType == REG_EXPAND_SZ)) {
                    const std::filesystem::path steamExecutable = std::filesystem::path(steamPath) / "steam.exe";
                    if (std::filesystem::is_regular_file(steamExecutable)) {
                        return steamExecutable;
                    }
                }
            }
#endif
            const std::filesystem::path inferredRoot = hitmanDirectory.parent_path().parent_path().parent_path();
            const std::filesystem::path inferredSteamExecutable = inferredRoot / "steam.exe";
            require(std::filesystem::is_regular_file(inferredSteamExecutable),
                "Could not locate Steam.exe from the Steam registry setting or HITMAN installation path.");
            return inferredSteamExecutable;
        }

#ifndef _WIN32
        wxExecuteEnv inheritedLaunchEnvironment(const std::filesystem::path& steamExecutable) {
            wxExecuteEnv environment;
            environment.cwd = wxString(steamExecutable.parent_path().wstring());
#ifdef _WIN32
            LPWCH environmentBlock = GetEnvironmentStringsW();
            require(environmentBlock != nullptr, "Could not read the current process environment.");
            for (const wchar_t* entry = environmentBlock; *entry != L'\0';) {
                const std::wstring variable(entry);
                entry += variable.size() + 1;
                const size_t equals = variable.find(L'=');
                if (equals != std::wstring::npos && equals != 0) {
                    environment.env[wxString(variable.substr(0, equals))] = wxString(variable.substr(equals + 1));
                }
            }
            FreeEnvironmentStringsW(environmentBlock);
#endif
            environment.env["SteamGameId"] = "1659040";
            environment.env["SteamAppId"] = "1659040";
            environment.env["SteamOverlayGameId"] = "1659040";
            return environment;
        }
#else
        /// The current environment with Steam's game variables for HITMAN 3 set, as a CreateProcess block.
        std::wstring steamLaunchEnvironment() {
            std::map<std::wstring, std::wstring> variables;
            LPWCH environmentBlock = GetEnvironmentStringsW();
            require(environmentBlock != nullptr, "Could not read the current process environment.");
            for (const wchar_t* entry = environmentBlock; *entry != L'\0';) {
                const std::wstring variable(entry);
                entry += variable.size() + 1;
                const size_t equals = variable.find(L'=', 1);
                if (equals != std::wstring::npos) {
                    variables[variable.substr(0, equals)] = variable.substr(equals + 1);
                }
            }
            FreeEnvironmentStringsW(environmentBlock);
            for (const wchar_t* name : {L"SteamGameId", L"SteamAppId", L"SteamOverlayGameId"}) {
                variables[name] = L"1659040";
            }
            std::wstring block;
            for (const auto& [name, value] : variables) {
                block += name + L"=" + value;
                block.push_back(L'\0');
            }
            block.push_back(L'\0');
            return block;
        }
#endif

        bool waitForNextAttempt(const Deadline deadline, const std::chrono::seconds delay = std::chrono::seconds(5)) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(
                std::min(std::chrono::duration_cast<std::chrono::steady_clock::duration>(delay), deadline - now));
            return std::chrono::steady_clock::now() < deadline;
        }
    } // namespace

    void setLogSink(LogSink sink) {
        std::lock_guard lock(logMutex);
        logSink = std::move(sink);
    }

    void log(const LogLevel level, const std::string& message) {
        std::lock_guard lock(logMutex);
        if (logSink) {
            logSink(level, message);
            return;
        }
        switch (level) {
        case LogLevel::Detail:
        case LogLevel::Info:
            std::cout << message << '\n' << std::flush;
            break;
        case LogLevel::Warning:
            std::cerr << "[warn] " << message << '\n' << std::flush;
            break;
        case LogLevel::Error:
            std::cerr << "[fail] " << message << '\n' << std::flush;
            break;
        }
    }

    void setResourceDirectory(const std::filesystem::path& directory) {
        resourceDirectory = directory;
    }

    std::filesystem::path resourceFile(const std::string& name) {
        return resourceDirectory.empty() ? FileUtil::getApplicationResourcePath(name) : resourceDirectory / name;
    }

    void require(const bool condition, const std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    void configureConsoleWidth() {
#ifdef _WIN32
        const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO bufferInfo{};
        if (console == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(console, &bufferInfo)) {
            return;
        }

        const COORD largestWindow = GetLargestConsoleWindowSize(console);
        const SHORT targetWidth = std::min(pipelineConsoleWidth, largestWindow.X);
        if (targetWidth <= bufferInfo.srWindow.Right - bufferInfo.srWindow.Left + 1) {
            return;
        }

        SMALL_RECT window = bufferInfo.srWindow;
        window.Right = static_cast<SHORT>(window.Left + targetWidth - 1);
        const SHORT targetBufferWidth = std::max(targetWidth, bufferInfo.dwSize.X);
        if (targetBufferWidth > bufferInfo.dwSize.X) {
            SetConsoleScreenBufferSize(console, {targetBufferWidth, bufferInfo.dwSize.Y});
        }
        SetConsoleWindowInfo(console, TRUE, &window);
#endif
    }

    void requireNonEmptyFile(const std::filesystem::path& path, const std::string_view description) {
        require(std::filesystem::is_regular_file(path), std::string(description) + " was not created.");
        require(std::filesystem::file_size(path) > 0, std::string(description) + " is empty.");
    }

    ConfiguredPaths readConfiguredPaths(
        const std::filesystem::path& executablePath, const std::filesystem::path& defaultIniPath) {
        CSimpleIniA defaultIni;
        defaultIni.SetUnicode();
        require(defaultIni.LoadFile(defaultIniPath.string().c_str()) >= 0,
            "Could not load the default NavKit.ini at " + defaultIniPath.string() + ".");

        ConfiguredPaths paths{defaultIni.GetValue("NavKit", "hitman", ""), defaultIni.GetValue("NavKit", "blender", ""),
            defaultIni.GetValue("NavKit", "output", "")};

        const std::filesystem::path localIniPath = executablePath.parent_path() / "NavKit.ini";
        if (std::filesystem::exists(localIniPath)) {
            CSimpleIniA localIni;
            localIni.SetUnicode();
            require(localIni.LoadFile(localIniPath.string().c_str()) >= 0,
                "Could not load the executable's NavKit.ini at " + localIniPath.string() + ".");
            paths.hitman = localIni.GetValue("NavKit", "hitman", paths.hitman.c_str());
            paths.blender = localIni.GetValue("NavKit", "blender", paths.blender.c_str());
            paths.output = localIni.GetValue("NavKit", "output", paths.output.c_str());
        }
        return paths;
    }

    ScopedAutoLoadScene::ScopedAutoLoadScene(const std::filesystem::path& modsIni, const std::string& scenePath) :
        modsIni(modsIni) {
        originalSettings = updateAutoLoadScene(modsIni, {"auto_load_scene = " + scenePath});
    }

    ScopedAutoLoadScene::~ScopedAutoLoadScene() {
        if (!restored) {
            try {
                restore();
            } catch (const std::exception& error) {
                LogStream(LogLevel::Error) << "Could not restore auto_load_scene: " << error.what() << '\n';
            }
        }
    }

    void ScopedAutoLoadScene::restore() {
        updateAutoLoadScene(modsIni, originalSettings);
        restored = true;
    }

    void initRpkg(const std::filesystem::path& hitmanDirectory, const std::filesystem::path& blenderExecutable,
        const std::filesystem::path& outputDirectory) {
        require(std::filesystem::is_directory(hitmanDirectory), "The supplied Hitman directory does not exist.");
        require(std::filesystem::is_regular_file(blenderExecutable), "The supplied Blender executable does not exist.");
        require(std::filesystem::is_directory(hitmanDirectory / "Runtime"),
            "The supplied Hitman directory has no Runtime folder.");
        require(std::filesystem::is_directory(hitmanDirectory / "Retail"),
            "The supplied Hitman directory has no Retail folder.");
        std::filesystem::create_directories(outputDirectory);

        NavKitSettings& settings = NavKitSettings::getInstance();
        settings.hitmanFolder = hitmanDirectory.string();
        settings.outputFolder = outputDirectory.string();
        settings.blenderPath = blenderExecutable.string();

        if (Rpkg::partitionManager != nullptr) {
            free_partition_manager(Rpkg::partitionManager);
            Rpkg::partitionManager = nullptr;
            Rpkg::extractionDataInitComplete = false;
        }
        LogStream(LogLevel::Detail) << "Scanning the Hitman installation's RPKG files...\n" << std::flush;
        Rpkg::initExtractionData();
        require(Rpkg::canExtract() && Rpkg::partitionManager != nullptr,
            "RPKG initialization failed; ensure this is a supported Hitman installation.");
    }

    void launchHitmanAndWaitForEditor(
        const std::filesystem::path& hitmanDirectory, const std::string_view gameVersion, const Deadline deadline) {
        const std::filesystem::path hitmanExecutable = hitmanDirectory / "Retail" / "HITMAN3.exe";
        require(std::filesystem::is_regular_file(hitmanExecutable), "Could not find Retail\\HITMAN3.exe.");
        if (gameVersion != "steam") {
            throw std::runtime_error("Unsupported game version '" + std::string(gameVersion) + "'.");
        }
        const std::filesystem::path steamExecutable = findSteamExecutable(hitmanDirectory);
#ifdef _WIN32
        std::wstring environment = steamLaunchEnvironment();
        std::wstring commandLine = L"\"" + steamExecutable.wstring() + L"\" -applaunch 1659040";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        require(CreateProcessW(steamExecutable.wstring().c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                    CREATE_UNICODE_ENVIRONMENT, environment.data(), steamExecutable.parent_path().wstring().c_str(),
                    &startup, &process) != FALSE,
            "Could not start HITMAN 3 through Steam.exe -applaunch 1659040.");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
#else
        const wxExecuteEnv environment = inheritedLaunchEnvironment(steamExecutable);
        const wxString command = "\"" + wxString(steamExecutable.wstring()) + "\" -applaunch 1659040";
        const long processId = wxExecute(command, wxEXEC_ASYNC, nullptr, &environment);
        require(processId > 0, "Could not start HITMAN 3 through Steam.exe -applaunch 1659040.");
#endif
        LogStream(LogLevel::Detail) << "Requested HITMAN 3 launch through Steam; retrying the editor connection every "
                                       "5 seconds...\n"
                                    << std::flush;

        GameConnection& gameConnection = GameConnection::getInstance();
        size_t connectionAttempt = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            ++connectionAttempt;
            LogStream(LogLevel::Detail) << "Socket call: open editor connection (attempt " << connectionAttempt
                                        << ")...\n"
                                        << std::flush;
            if (gameConnection.connectToGame() == 0) {
                LogStream(LogLevel::Detail) << "Socket opened: ws://localhost:46735/socket\n" << std::flush;
                LogStream(LogLevel::Detail) << "Connected to the HITMAN editor endpoint.\n";
                return;
            }
            LogStream(LogLevel::Detail) << "Socket open failed; will retry.\n" << std::flush;
            waitForNextAttempt(deadline);
        }

        throw std::runtime_error("Could not connect to the HITMAN editor endpoint before the timeout.");
    }

    bool isHitmanRunning(const std::filesystem::path& hitmanDirectory) {
#ifdef _WIN32
        return !findHitmanProcesses(hitmanDirectory / "Retail" / "HITMAN3.exe").empty();
#else
        (void)hitmanDirectory;
        return false;
#endif
    }

    void closeHitmanGame(const std::filesystem::path& hitmanDirectory) {
#ifdef _WIN32
        const std::filesystem::path hitmanExecutable = hitmanDirectory / "Retail" / "HITMAN3.exe";
        const std::vector<DWORD> processIds = findHitmanProcesses(hitmanExecutable);
        for (const DWORD processId : processIds) {
            HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            require(process != nullptr, "Could not open the HITMAN process to request a normal shutdown.");

            CloseWindowContext context{processId};
            EnumWindows(requestWindowClose, reinterpret_cast<LPARAM>(&context));
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
                CloseHandle(process);
                continue;
            }
            require(context.closeRequested, "Could not find a visible HITMAN window to close.");
            LogStream(LogLevel::Detail) << "Requested HITMAN to close; waiting for it to exit...\n" << std::flush;
            const DWORD waitResult = WaitForSingleObject(process, 30000);
            CloseHandle(process);
            require(waitResult == WAIT_OBJECT_0, "HITMAN did not close within 30 seconds after the close request.");
        }
#else
        (void)hitmanDirectory;
        throw std::runtime_error("Closing the HITMAN game is only supported on Windows.");
#endif
    }

#ifdef _WIN32
    void terminateProcessTree(const unsigned long processId) {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            std::vector<DWORD> children;
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    if (entry.th32ParentProcessID == processId) {
                        children.push_back(entry.th32ProcessID);
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
            for (const DWORD child : children) {
                terminateProcessTree(child);
            }
        }
        if (const HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, processId); process != nullptr) {
            TerminateProcess(process, 1);
            CloseHandle(process);
        }
    }

    CommandResult runLoggedCommand(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
        const std::filesystem::path& workingDirectory, const std::filesystem::path& logFile,
        const std::chrono::minutes timeout, unsigned long& exitCode) {
        wchar_t systemDirectory[MAX_PATH]{};
        require(GetSystemDirectoryW(systemDirectory, MAX_PATH) != 0, "Could not find the Windows system folder.");
        const std::filesystem::path cmd = std::filesystem::path(systemDirectory) / "cmd.exe";
        // cmd /s /c strips the outer quotes and runs the rest as written.
        std::wstring command = L"\"" + executable.wstring() + L"\"";
        for (const std::wstring& argument : arguments) {
            command += L" \"" + argument + L"\"";
        }
        std::wstring commandLine =
            L"\"" + cmd.wstring() + L"\" /d /s /c \"" + command + L" >> \"" + logFile.wstring() + L"\" 2>&1 < NUL\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const BOOL started = CreateProcessW(cmd.wstring().c_str(), commandLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, workingDirectory.wstring().c_str(), &startup, &process);
        require(started != FALSE, "Could not start " + executable.string() + ".");
        CloseHandle(process.hThread);
        const DWORD waitResult =
            WaitForSingleObject(process.hProcess, static_cast<DWORD>(std::chrono::milliseconds(timeout).count()));
        if (waitResult == WAIT_TIMEOUT) {
            terminateProcessTree(process.dwProcessId);
            WaitForSingleObject(process.hProcess, 30000);
            CloseHandle(process.hProcess);
            return CommandResult::TimedOut;
        }
        DWORD code = 0;
        GetExitCodeProcess(process.hProcess, &code);
        exitCode = code;
        CloseHandle(process.hProcess);
        return CommandResult::Finished;
    }
#endif

    void extractSceneFromRunningGame(
        const std::filesystem::path& outputDirectory, const Deadline deadline, const std::chrono::seconds settleTime) {
        const std::filesystem::path sceneFile = outputDirectory / Scene::OUTPUT_SCENE_FILE_NAME;
        const std::filesystem::path previousSceneFile = outputDirectory / "output.previous.nav.json";
        std::filesystem::remove(previousSceneFile);
        // The editor writes the scene to NavKit's output folder.
        NavKitSettings::getInstance().outputFolder = outputDirectory.string();
        GameConnection& gameConnection = GameConnection::getInstance();
        Scene& scene = Scene::getInstance();
        bool useExistingConnection = true;
        size_t extractionAttempt = 0;
        size_t previousMeshCount = 0;
        bool waitLonger = false;
        if (settleTime.count() > 0) {
            LogStream(LogLevel::Detail) << "Waiting " << settleTime.count()
                                        << " seconds for the mission to load before requesting the scene...\n"
                                        << std::flush;
            waitForNextAttempt(deadline, settleTime);
        }
        while (std::chrono::steady_clock::now() < deadline) {
            ++extractionAttempt;
            const bool extracted = NavKit::ProcessFlow::extractScene(
                sceneFile,
                [&gameConnection, &useExistingConnection, &extractionAttempt] {
                    if (useExistingConnection) {
                        useExistingConnection = false;
                        LogStream(LogLevel::Detail) << "Socket call: reuse open editor connection (scene attempt "
                                                    << extractionAttempt << ").\n"
                                                    << std::flush;
                        return 0;
                    }
                    LogStream(LogLevel::Detail)
                        << "Socket call: open editor connection (scene attempt " << extractionAttempt << ")...\n"
                        << std::flush;
                    const int result = gameConnection.connectToGame();
                    if (result == 0) {
                        LogStream(LogLevel::Detail) << "Socket opened: ws://localhost:46735/socket\n" << std::flush;
                    } else {
                        LogStream(LogLevel::Detail) << "Socket open failed.\n" << std::flush;
                    }
                    return result;
                },
                [&gameConnection, &extractionAttempt] {
                    LogStream(LogLevel::Detail)
                        << "Socket call: request scene entities (scene attempt " << extractionAttempt << ").\n"
                        << std::flush;
                    const int result = gameConnection.listNavKitSceneEntities();
                    LogStream(LogLevel::Detail) << "Socket call complete: scene entity request "
                                                << (result == 0 ? "succeeded" : "failed") << ".\n"
                                                << std::flush;
                    return result;
                },
                [&gameConnection] {
                    LogStream(LogLevel::Detail) << "Socket call: close editor connection...\n" << std::flush;
                    const int result = gameConnection.closeConnection();
                    LogStream(LogLevel::Detail) << "Socket " << (result == 0 ? "closed" : "close failed") << ".\n"
                                                << std::flush;
                    return result;
                });

            if (extracted && std::filesystem::is_regular_file(sceneFile) && std::filesystem::file_size(sceneFile) > 0) {
                scene.lastLoadSceneFile = sceneFile.string();
                scene.loadScene(
                    scene.lastLoadSceneFile, [] {},
                    [] {
                        throw std::runtime_error("Could not parse the scene JSON extracted from the running game.");
                    });
                scene.sceneLoaded = true;
                // Meshes stream in while the mission loads, so only accept a scene once two extractions agree.
                // Dynamic entities make the count drift slightly in a loaded mission, so allow 0.5%.
                const size_t meshCount = scene.meshes.size();
                const size_t difference =
                    meshCount > previousMeshCount ? meshCount - previousMeshCount : previousMeshCount - meshCount;
                if (meshCount != 0 && previousMeshCount != 0 && difference * 200 <= meshCount) {
                    return;
                }
                if (scene.meshes.empty()) {
                    LogStream(LogLevel::Detail)
                        << "No mesh entities yet; the mission may still be loading. Retrying...\n"
                        << std::flush;
                    waitLonger = true;
                } else {
                    LogStream(LogLevel::Detail)
                        << "The scene has " << scene.meshes.size()
                        << " meshes; extracting again to check the mission has finished loading...\n"
                        << std::flush;
                    std::error_code copyError;
                    std::filesystem::copy_file(
                        sceneFile, previousSceneFile, std::filesystem::copy_options::overwrite_existing, copyError);
                }
                previousMeshCount = scene.meshes.size();
            } else {
                LogStream(LogLevel::Detail) << "Scene extraction did not complete; retrying...\n" << std::flush;
            }

            const std::chrono::seconds delay =
                waitLonger && settleTime.count() > 0 ? settleTime : std::chrono::seconds(5);
            waitLonger = false;
            if (!waitForNextAttempt(deadline, delay)) {
                break;
            }
        }

        // Some missions (the Himmelstein sniper map) crash after answering one scene request; use that answer.
        if (previousMeshCount > 0 && std::filesystem::is_regular_file(previousSceneFile)) {
            std::filesystem::copy_file(previousSceneFile, sceneFile, std::filesystem::copy_options::overwrite_existing);
            scene.lastLoadSceneFile = sceneFile.string();
            scene.loadScene(
                scene.lastLoadSceneFile, [] {},
                [] { throw std::runtime_error("Could not parse the scene JSON extracted from the running game."); });
            scene.sceneLoaded = true;
            if (!scene.meshes.empty()) {
                LogStream(LogLevel::Warning)
                    << "The game stopped answering before a second extraction could confirm the mission had "
                       "finished loading; using the first extraction ("
                    << scene.meshes.size() << " meshes).";
                return;
            }
        }
        throw std::runtime_error(
            "The active mission returned no mesh entities, or scene extraction failed, before the timeout.");
    }

    namespace {
        /// Runs Glacier2Glb.py on the scene in @p outputDirectory; @p outputFile's extension selects what it writes.
        void runGlacier2Glb(const std::filesystem::path& blenderExecutable,
            const std::filesystem::path& outputDirectory, const std::filesystem::path& outputFile) {
            const std::filesystem::path sceneFile = outputDirectory / Scene::OUTPUT_SCENE_FILE_NAME;
            const std::filesystem::path scriptFile = resourceFile("Glacier2Glb.py");
            require(std::filesystem::is_regular_file(scriptFile), "Could not find " + scriptFile.string() + ".");
            const std::filesystem::path blenderLog = outputDirectory / "blender.log";
#ifdef _WIN32
            unsigned long exitCode = 0;
            const CommandResult blenderResult = runLoggedCommand(blenderExecutable,
                {L"-b", L"--factory-startup", L"-P", scriptFile.wstring(), L"--", sceneFile.wstring(),
                    outputFile.wstring(), L"ALOC", L"11111111", L"copy", L"true", L"false", L"false"},
                outputDirectory, blenderLog, std::chrono::minutes(90), exitCode);
            require(blenderResult == CommandResult::Finished,
                "Blender did not finish within 90 minutes; see " + blenderLog.string() + ".");
            require(exitCode == 0,
                "Blender failed while running Glacier2Glb.py (exit code " + std::to_string(exitCode) + "); see " +
                    blenderLog.string() + ".");
#else
            const std::vector<wxString> blenderArguments = {pathToWxString(blenderExecutable), "-b",
                "--factory-startup", "-P", pathToWxString(scriptFile), "--", pathToWxString(sceneFile),
                pathToWxString(outputFile), "ALOC", "11111111", "copy", "true", "false", "false"};
            std::vector<const wxChar*> blenderArgumentPointers;
            blenderArgumentPointers.reserve(blenderArguments.size() + 1);
            for (const wxString& argument : blenderArguments) {
                blenderArgumentPointers.push_back(argument.c_str());
            }
            blenderArgumentPointers.push_back(nullptr);
            require(wxExecute(blenderArgumentPointers.data(), wxEXEC_SYNC) == 0,
                "Blender failed while running Glacier2Glb.py.");
#endif
        }
    } // namespace

    std::filesystem::path buildBlendFile(
        const std::filesystem::path& blenderExecutable, const std::filesystem::path& outputDirectory) {
        const std::filesystem::path blendFile = outputDirectory / "output.blend";
        runGlacier2Glb(blenderExecutable, outputDirectory, blendFile);
        requireNonEmptyFile(blendFile, "Blender-generated .blend");
        return blendFile;
    }

    void extractAssetsAndBuildGlb(const std::filesystem::path& hitmanDirectory,
        const std::filesystem::path& blenderExecutable, const std::filesystem::path& outputDirectory,
        const bool alsoWriteGlb) {
        const std::filesystem::path sceneFile = outputDirectory / Scene::OUTPUT_SCENE_FILE_NAME;
        const std::filesystem::path runtimeDirectory = hitmanDirectory / "Runtime";
        const std::filesystem::path alocDirectory = outputDirectory / "aloc";
        const std::filesystem::path generatedGltf = outputDirectory / "output.gltf";
        std::filesystem::create_directories(alocDirectory);

        require(Rpkg::extractionDataInitComplete && Rpkg::partitionManager != nullptr,
            "RPKG extraction data was not initialized from the Hitman installation.");
        SceneMesh& sceneMesh = SceneMesh::getInstance();
        sceneMesh.meshTypeForBuild = ALOC;
        sceneMesh.sceneMeshBuildType = COPY;
        sceneMesh.onlyCollidable = true;
        const int extractionResult =
            extract_scene_mesh_resources(sceneFile.string().c_str(), runtimeDirectory.string().c_str(),
                Rpkg::partitionManager, alocDirectory.string().c_str(), "ALOC", Logger::rustLogCallback);
        require(extractionResult == 0, "navkit-rpkg-lib failed to extract scene ALOC resources.");
        require(std::filesystem::directory_iterator(alocDirectory) != std::filesystem::directory_iterator{},
            "RPKG extraction produced no ALOC files.");

        runGlacier2Glb(
            blenderExecutable, outputDirectory, alsoWriteGlb ? outputDirectory / "output.gltfAndGlb" : generatedGltf);
        requireNonEmptyFile(generatedGltf, "Blender-generated glTF");
        if (alsoWriteGlb) {
            requireNonEmptyFile(outputDirectory / "output.glb", "Blender-generated GLB");
        }

        RecastAdapter& adapter = RecastAdapter::getInstance();
        adapter.resetCommonSettings();
        adapter.cleanup();
        require(adapter.loadInputGeom(generatedGltf.string()), "Recast could not load the Blender-generated glTF.");
        adapter.handleMeshChanged();
        sceneMesh.glbLoaded = true;
    }

    std::filesystem::path buildNavp(const std::filesystem::path& outputDirectory) {
        RecastAdapter& adapter = RecastAdapter::getInstance();
        Scene& scene = Scene::getInstance();
        const float bboxMin[3] = {scene.bBoxPos[0] - scene.bBoxScale[0] / 2, scene.bBoxPos[1] - scene.bBoxScale[1] / 2,
            scene.bBoxPos[2] - scene.bBoxScale[2] / 2};
        const float bboxMax[3] = {scene.bBoxPos[0] + scene.bBoxScale[0] / 2, scene.bBoxPos[1] + scene.bBoxScale[1] / 2,
            scene.bBoxPos[2] + scene.bBoxScale[2] / 2};
        adapter.setMeshBBox(bboxMin, bboxMax);
        Navp::updateExclusionBoxConvexVolumes();
        require(adapter.handleBuild(), "Recast failed to build a NAVP from the generated GLB.");
        adapter.findPfSeedPointAreas();
        adapter.excludeNonReachableAreas();

        const std::filesystem::path navpJson = outputDirectory / "output.navp.json";
        adapter.save(navpJson.string(), false);
        requireNonEmptyFile(navpJson, "Recast-generated NAVP JSON");

        Navp& navp = Navp::getInstance();
        navp.loadNavMesh(navpJson.string(), true, true, false);
        require(navp.navpLoaded, "NavKit could not load the Recast-generated NAVP JSON.");

        const std::filesystem::path navpBinary = outputDirectory / "output.navp";
        navp.saveNavMesh(navpBinary.string(), "NAVP");
        requireNonEmptyFile(navpBinary, "Serialized NAVP");

        navp.loadNavMesh(navpBinary.string(), false, false, false);
        require(navp.navpLoaded && Navp::getTotalAreaCount(navp.navMesh) > 0,
            "Serialized NAVP could not be loaded or contained no navigation areas.");
        return navpBinary;
    }

    std::filesystem::path buildAirg(const std::filesystem::path& outputDirectory) {
        RecastAdapter::getAirgInstance().resetCommonSettings();
        Airg::resetDefaults();
        require(GridGenerator::getInstance().build(), "GridGenerator failed to build an AIRG from the NAVP.");

        const std::filesystem::path airgFile = outputDirectory / "output.airg";
        Airg& airg = Airg::getInstance();
        airg.reasoningGrid->writeAirg(airgFile);
        requireNonEmptyFile(airgFile, "Serialized AIRG");

        ReasoningGrid loadedGrid;
        loadedGrid.readAirg(airgFile);
        require(!loadedGrid.m_WaypointList.empty(), "Serialized AIRG contained no waypoints.");
        require(loadedGrid.m_nNodeCount == loadedGrid.m_WaypointList.size(),
            "Serialized AIRG node count did not match its waypoint count.");
        return airgFile;
    }
} // namespace NavKit::LivePipeline
