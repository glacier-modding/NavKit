#include "../include/NavKit/adapter/RecastAdapter.h"
#include "../include/NavKit/module/Airg.h"
#include "../include/NavKit/module/GameConnection.h"
#include "../include/NavKit/module/Grid.h"
#include "../include/NavKit/module/Logger.h"
#include "../include/NavKit/module/NavKitSettings.h"
#include "../include/NavKit/module/Navp.h"
#include "../include/NavKit/module/Rpkg.h"
#include "../include/NavKit/module/Scene.h"
#include "../include/NavKit/module/SceneMesh.h"
#include "../include/NavKit/module/ProcessFlow.h"
#include "../include/NavKit/util/GridGenerator.h"
#include "../include/navkit-rpkg-lib/navkit-rpkg-lib.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <conio.h>
#include <tlhelp32.h>
#include <windows.h>
#endif
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <SimpleIni.h>

namespace {
    constexpr SHORT integrationConsoleWidth = 240;

    struct IntegrationArguments {
        std::filesystem::path hitmanDirectory;
        std::filesystem::path blenderExecutable;
        std::filesystem::path outputDirectory;
        std::string gameVersion = "steam";
        bool interactiveMode = false;
    };

    void require(const bool condition, const std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

#ifdef _WIN32
    void configureConsoleWidth() {
        const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO bufferInfo{};
        if (console == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(console, &bufferInfo)) {
            return;
        }

        const COORD largestWindow = GetLargestConsoleWindowSize(console);
        const SHORT targetWidth = std::min(integrationConsoleWidth, largestWindow.X);
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
    }
#endif

    wxString pathToWxString(const std::filesystem::path& path) {
        return wxString(path.wstring());
    }

#ifdef _WIN32
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
                    CompareStringOrdinal(expectedPath.c_str(), -1, executablePath.data(), static_cast<int>(pathLength),
                        TRUE) == CSTR_EQUAL) {
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
            std::cout << "[integration] Requested HITMAN to close; waiting for it to exit...\n" << std::flush;
            const DWORD waitResult = WaitForSingleObject(process, 30000);
            CloseHandle(process);
            require(waitResult == WAIT_OBJECT_0, "HITMAN did not close within 30 seconds after the close request.");
        }
#else
        (void)hitmanDirectory;
        throw std::runtime_error("Closing the HITMAN game is only supported on Windows.");
#endif
    }

    std::vector<char> readFile(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        require(stream.is_open(), "Could not open an integration-test output.");
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    void requireNonEmptyFile(const std::filesystem::path& path, const std::string_view description) {
        require(std::filesystem::is_regular_file(path), std::string(description) + " was not created.");
        require(std::filesystem::file_size(path) > 0, std::string(description) + " is empty.");
    }

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
                        updatedLines.insert(updatedLines.end(), replacementSettings.begin(), replacementSettings.end());
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

    class ScopedAutoLoadScene {
    public:
        explicit ScopedAutoLoadScene(const std::filesystem::path& modsIni) : modsIni(modsIni) {
            originalSettings = updateAutoLoadScene(modsIni,
                {"auto_load_scene = "
                 "assembly:/_pro/scenes/missions/thefacility/_scene_mission_polarbear_intro_firsttime.entity"});
        }

        ~ScopedAutoLoadScene() {
            if (!restored) {
                try {
                    restore();
                } catch (const std::exception& error) {
                    std::cerr << "[fail] Could not restore auto_load_scene: " << error.what() << '\n';
                }
            }
        }

        void restore() {
            updateAutoLoadScene(modsIni, originalSettings);
            restored = true;
        }

    private:
        std::filesystem::path modsIni;
        std::vector<std::string> originalSettings;
        bool restored = false;
    };

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

    using IntegrationDeadline = std::chrono::steady_clock::time_point;

    bool waitForNextAttempt(const IntegrationDeadline deadline) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::min(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(5)), deadline - now));
        return std::chrono::steady_clock::now() < deadline;
    }

    void launchHitmanAndWaitForEditor(const std::filesystem::path& hitmanDirectory, const std::string_view gameVersion,
        const IntegrationDeadline deadline) {
        const std::filesystem::path hitmanExecutable = hitmanDirectory / "Retail" / "HITMAN3.exe";
        require(std::filesystem::is_regular_file(hitmanExecutable), "Could not find Retail\\HITMAN3.exe.");
        if (gameVersion != "steam") {
            throw std::runtime_error("Unsupported game version '" + std::string(gameVersion) + "'.");
        }
        const std::filesystem::path steamExecutable = findSteamExecutable(hitmanDirectory);
        const wxExecuteEnv environment = inheritedLaunchEnvironment(steamExecutable);
        const wxString command = "\"" + wxString(steamExecutable.wstring()) + "\" -applaunch 1659040";
        const long processId = wxExecute(command, wxEXEC_ASYNC, nullptr, &environment);
        require(processId > 0, "Could not start HITMAN 3 through Steam.exe -applaunch 1659040.");
        std::cout << "[integration] Requested HITMAN 3 launch through Steam; retrying the editor connection every "
                     "5 seconds for up to 2 minutes...\n"
                  << std::flush;

        GameConnection& gameConnection = GameConnection::getInstance();
        size_t connectionAttempt = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            ++connectionAttempt;
            std::cout << "[integration] Socket call: open editor connection (attempt " << connectionAttempt << ")...\n"
                      << std::flush;
            if (gameConnection.connectToGame() == 0) {
                std::cout << "[integration] Socket opened: ws://localhost:46735/socket\n" << std::flush;
                std::cout << "[integration] Connected to the HITMAN editor endpoint.\n";
                return;
            }
            std::cout << "[integration] Socket open failed; will retry.\n" << std::flush;
            waitForNextAttempt(deadline);
        }

        throw std::runtime_error("Could not connect to the HITMAN editor endpoint within 2 minutes.");
    }

    std::filesystem::path makeRunDirectory(const std::filesystem::path& outputRoot) {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const std::filesystem::path runDirectory = outputRoot / ("integration-" + std::to_string(stamp));
        std::filesystem::create_directories(runDirectory);
        return runDirectory;
    }

    void extractSceneFromRunningGame(const std::filesystem::path& outputDirectory, const IntegrationDeadline deadline) {
        const std::filesystem::path sceneFile = outputDirectory / Scene::OUTPUT_SCENE_FILE_NAME;
        GameConnection& gameConnection = GameConnection::getInstance();
        Scene& scene = Scene::getInstance();
        bool useExistingConnection = true;
        size_t extractionAttempt = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            ++extractionAttempt;
            const bool extracted = NavKit::ProcessFlow::extractScene(
                sceneFile,
                [&gameConnection, &useExistingConnection, &extractionAttempt] {
                    if (useExistingConnection) {
                        useExistingConnection = false;
                        std::cout << "[integration] Socket call: reuse open editor connection (scene attempt "
                                  << extractionAttempt << ").\n"
                                  << std::flush;
                        return 0;
                    }
                    std::cout << "[integration] Socket call: open editor connection (scene attempt "
                              << extractionAttempt << ")...\n"
                              << std::flush;
                    const int result = gameConnection.connectToGame();
                    if (result == 0) {
                        std::cout << "[integration] Socket opened: ws://localhost:46735/socket\n" << std::flush;
                    } else {
                        std::cout << "[integration] Socket open failed.\n" << std::flush;
                    }
                    return result;
                },
                [&gameConnection, &extractionAttempt] {
                    std::cout << "[integration] Socket call: request scene entities (scene attempt "
                              << extractionAttempt << ").\n"
                              << std::flush;
                    const int result = gameConnection.listNavKitSceneEntities();
                    std::cout << "[integration] Socket call complete: scene entity request "
                              << (result == 0 ? "succeeded" : "failed") << ".\n"
                              << std::flush;
                    return result;
                },
                [&gameConnection] {
                    std::cout << "[integration] Socket call: close editor connection...\n" << std::flush;
                    const int result = gameConnection.closeConnection();
                    std::cout << "[integration] Socket " << (result == 0 ? "closed" : "close failed") << ".\n"
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
                if (!scene.meshes.empty()) {
                    return;
                }
                std::cout << "[integration] No mesh entities yet; the mission may still be loading. Retrying...\n"
                          << std::flush;
            } else {
                std::cout << "[integration] Scene extraction did not complete; retrying...\n" << std::flush;
            }

            if (!waitForNextAttempt(deadline)) {
                break;
            }
        }

        throw std::runtime_error(
            "The active mission returned no mesh entities, or scene extraction failed, within 2 minutes.");
    }

    void extractAssetsAndBuildGlb(const std::filesystem::path& hitmanDirectory,
        const std::filesystem::path& blenderExecutable, const std::filesystem::path& outputDirectory) {
        const std::filesystem::path sceneFile = outputDirectory / Scene::OUTPUT_SCENE_FILE_NAME;
        const std::filesystem::path runtimeDirectory = hitmanDirectory / "Runtime";
        const std::filesystem::path alocDirectory = outputDirectory / "aloc";
        const std::filesystem::path generatedGltf = outputDirectory / "output.gltf";
        const std::filesystem::path scriptFile =
            std::filesystem::u8path(NAVKIT_TEST_SOURCE_DIR) / "src" / "resource" / "Glacier2Glb.py";
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

        const std::vector<wxString> blenderArguments = {pathToWxString(blenderExecutable), "-b", "--factory-startup",
            "-P", pathToWxString(scriptFile), "--", pathToWxString(sceneFile), pathToWxString(generatedGltf), "ALOC",
            "11111111", "copy", "true", "false"};
        std::vector<const wxChar*> blenderArgumentPointers;
        blenderArgumentPointers.reserve(blenderArguments.size() + 1);
        for (const wxString& argument : blenderArguments) {
            blenderArgumentPointers.push_back(argument.c_str());
        }
        blenderArgumentPointers.push_back(nullptr);
        require(wxExecute(blenderArgumentPointers.data(), wxEXEC_SYNC) == 0,
            "Blender failed while running the repository's Glacier2Glb.py script.");
        requireNonEmptyFile(generatedGltf, "Blender-generated glTF");

        RecastAdapter& adapter = RecastAdapter::getInstance();
        adapter.resetCommonSettings();
        adapter.cleanup();
        require(adapter.loadInputGeom(generatedGltf.string()), "Recast could not load the Blender-generated glTF.");
        adapter.handleMeshChanged();
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

    void buildAirg(const std::filesystem::path& outputDirectory) {
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
    }

    IntegrationArguments resolveArguments(
        const int argc, char** argv, const std::filesystem::path& executablePath, bool& interactiveMode) {
        std::vector<std::string> positionalArguments;
        std::string gameVersion = "steam";
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--gameversion") {
                require(index + 1 < argc, "--gameversion requires a value; supported value: steam.");
                gameVersion = argv[++index];
            } else if (argument.starts_with("--gameversion=")) {
                gameVersion = argument.substr(std::string_view("--gameversion=").size());
            } else if (argument == "--interactivemode") {
                require(index + 1 < argc, "--interactivemode requires true or false.");
                const std::string_view value(argv[++index]);
                require(value == "true" || value == "false", "--interactivemode only accepts true or false.");
                interactiveMode = value == "true";
            } else if (argument.starts_with("--interactivemode=")) {
                const std::string_view value = argument.substr(std::string_view("--interactivemode=").size());
                require(value == "true" || value == "false", "--interactivemode only accepts true or false.");
                interactiveMode = value == "true";
            } else if (argument.starts_with("--")) {
                throw std::runtime_error("Unknown option '" + std::string(argument) + "'.");
            } else {
                positionalArguments.emplace_back(argument);
            }
        }
        require(positionalArguments.size() <= 3,
            "Usage: NavKitIntegrationTests [Hitman installation directory] [blender.exe] [output directory] "
            "[--gameversion steam] [--interactivemode true|false]");
        require(gameVersion == "steam", "Unsupported game version '" + gameVersion + "'; only 'steam' is supported.");

        const std::filesystem::path defaultIniPath =
            std::filesystem::u8path(NAVKIT_TEST_SOURCE_DIR) / "src" / "resource" / "NavKit.ini";
        CSimpleIniA defaultIni;
        defaultIni.SetUnicode();
        require(defaultIni.LoadFile(defaultIniPath.string().c_str()) >= 0,
            "Could not load the default NavKit.ini at " + defaultIniPath.string() + ".");

        std::string hitman = defaultIni.GetValue("NavKit", "hitman", "");
        std::string blender = defaultIni.GetValue("NavKit", "blender", "");
        std::string output = defaultIni.GetValue("NavKit", "output", "");

        const std::filesystem::path localIniPath = executablePath.parent_path() / "NavKit.ini";
        if (std::filesystem::exists(localIniPath)) {
            CSimpleIniA localIni;
            localIni.SetUnicode();
            require(localIni.LoadFile(localIniPath.string().c_str()) >= 0,
                "Could not load the executable's NavKit.ini at " + localIniPath.string() + ".");
            hitman = localIni.GetValue("NavKit", "hitman", hitman.c_str());
            blender = localIni.GetValue("NavKit", "blender", blender.c_str());
            output = localIni.GetValue("NavKit", "output", output.c_str());
        }

        if (positionalArguments.size() > 0) {
            hitman = positionalArguments[0];
        }
        if (positionalArguments.size() > 1) {
            blender = positionalArguments[1];
        }
        if (positionalArguments.size() > 2) {
            output = positionalArguments[2];
        }

        require(!hitman.empty(), "No Hitman installation path is configured in NavKit.ini or on the command line.");
        require(!blender.empty(), "No Blender executable path is configured in NavKit.ini or on the command line.");
        require(!output.empty(), "No output directory is configured in NavKit.ini or on the command line.");

        return {std::filesystem::u8path(hitman), std::filesystem::u8path(blender), std::filesystem::u8path(output),
            gameVersion, interactiveMode};
    }

    void runIntegration(const std::filesystem::path& hitmanDirectory, const std::filesystem::path& blenderExecutable,
        const std::filesystem::path& outputRoot, const std::string_view gameVersion) {
        require(std::filesystem::is_directory(hitmanDirectory), "The supplied Hitman directory does not exist.");
        require(std::filesystem::is_regular_file(blenderExecutable), "The supplied Blender executable does not exist.");
        require(std::filesystem::is_directory(hitmanDirectory / "Runtime"),
            "The supplied Hitman directory has no Runtime folder.");
        require(std::filesystem::is_directory(hitmanDirectory / "Retail"),
            "The supplied Hitman directory has no Retail folder.");
        std::filesystem::create_directories(outputRoot);

        NavKitSettings& settings = NavKitSettings::getInstance();
        settings.hitmanFolder = hitmanDirectory.string();
        settings.outputFolder = outputRoot.string();
        settings.blenderPath = blenderExecutable.string();

        std::cout << "[integration] Scanning the Hitman installation's RPKG files...\n";
        Rpkg::initExtractionData();
        require(Rpkg::canExtract() && Rpkg::partitionManager != nullptr,
            "RPKG initialization failed; ensure this is a supported Hitman installation.");

        ScopedAutoLoadScene autoLoadScene(hitmanDirectory / "Retail" / "mods.ini");
        constexpr auto integrationTimeout = std::chrono::minutes(2);
        const IntegrationDeadline deadline = std::chrono::steady_clock::now() + integrationTimeout;
        launchHitmanAndWaitForEditor(hitmanDirectory, gameVersion, deadline);

        const std::filesystem::path outputDirectory = makeRunDirectory(outputRoot);
        settings.outputFolder = outputDirectory.string();
        std::cout << "[integration] Extracting the active mission from HITMAN...\n";
        extractSceneFromRunningGame(outputDirectory, deadline);
        closeHitmanGame(hitmanDirectory);
        std::cout << "[integration] Extracting ALOC resources and running Glacier2Glb.py...\n";
        extractAssetsAndBuildGlb(hitmanDirectory, blenderExecutable, outputDirectory);
        std::cout << "[integration] Building and validating NAVP...\n";
        buildNavp(outputDirectory);
        std::cout << "[integration] Building and validating AIRG...\n";
        buildAirg(outputDirectory);
        autoLoadScene.restore();
        std::cout << "[integration] Passed. Artifacts are in " << outputDirectory.string() << '\n';
    }
} // namespace

int main(const int argc, char** argv) {
#ifdef _WIN32
    configureConsoleWidth();
#endif
    int result = 0;
    bool interactiveMode = false;
    wxInitializer wx;
    if (!wx) {
        std::cerr << "[fail] Could not initialize wxWidgets.\n";
        result = 1;
    } else {
        try {
            const std::filesystem::path executablePath =
                std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring());
            const IntegrationArguments arguments = resolveArguments(argc, argv, executablePath, interactiveMode);
            std::cout << "[integration] HITMAN: " << arguments.hitmanDirectory.string() << '\n'
                      << "[integration] Blender: " << arguments.blenderExecutable.string() << '\n'
                      << "[integration] Output: " << arguments.outputDirectory.string() << '\n'
                      << "[integration] Game version: " << arguments.gameVersion << '\n';
            runIntegration(arguments.hitmanDirectory, arguments.blenderExecutable, arguments.outputDirectory,
                arguments.gameVersion);
            std::cout << "[pass] End-to-end integration test completed successfully.\n";
        } catch (const std::exception& error) {
            std::cerr << "[fail] " << error.what() << '\n';
            result = 1;
        }
    }

    if (interactiveMode) {
        std::cout << "Press any key to close" << std::flush;
#ifdef _WIN32
        _getch();
#else
        std::cin.get();
#endif
        std::cout << '\n';
    }
    return result;
}
