// Regenerates the NavKit Demo Mod: for each vanilla mission in the mod's mission list, launches HITMAN on the scene,
// extracts it, builds a NAVP, AIRG and one CRMD per crowd with NavKit, and writes them into the mod under the vanilla
// resource hashes so Simple Mod Framework replaces the official files. Optionally deploys the mod, checks the deployed
// RPKGs hold the generated files, and loads each mission in game as a smoke test.

#include "../include/NavKit/bulk/BulkGenerator.h"
#include "../include/NavKit/bulk/LivePipeline.h"
#include "../include/NavKit/module/Logger.h"
#include "../include/NavKit/module/Rpkg.h"
#include "../include/NavKit/NavKitConfig.h"
#include "../include/navkit-rpkg-lib/navkit-rpkg-lib.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <vector>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#include <tlhelp32.h>
#endif
#include <glm/glm.hpp>
#include <simdjson.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace {
    using namespace NavKit::LivePipeline;
    using namespace NavKit::Bulk;

    constexpr std::string_view missionsFileName = "navkit-demo-missions.json";
    constexpr std::string_view generatedManifestFileName = "navkit-demo-mod.generated.json";

    struct Arguments {
        std::filesystem::path hitmanDirectory;
        std::filesystem::path blenderExecutable;
        std::filesystem::path outputDirectory;
        std::filesystem::path modDirectory;
        std::filesystem::path missionsFile;
        std::vector<std::string> missionFilters;
        std::vector<std::string> excludeFilters;
        bool onlyMissing = false;
        std::string gameVersion = "steam";
        bool crmd = true;
        bool keepSceneFiles = false;
        bool deploy = true;
        bool smokeTest = true;
        bool dryRun = false;
        bool discover = false;
        bool generate = true;
        bool interactiveMode = false;
    };

    /// Reports the current stage every few minutes, and if one stage runs far too long, closes HITMAN and Blender,
    /// restores mods.ini and exits, so a stalled run fails visibly instead of waiting forever. The manifest is saved
    /// after each mission, and the next run clears an unfinished mission's folders.
    class Watchdog {
    public:
        static constexpr auto reportInterval = std::chrono::minutes(5);
        static constexpr auto stageLimit = std::chrono::minutes(90);

        ~Watchdog() {
            stop();
        }

        void start(const std::filesystem::path& hitmanDirectory) {
            this->hitmanDirectory = hitmanDirectory;
            const std::filesystem::path modsIni = hitmanDirectory / "Retail" / "mods.ini";
            if (std::filesystem::is_regular_file(modsIni)) {
                std::ifstream input(modsIni, std::ios::binary);
                modsIniContent = std::string(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
            }
            running = true;
            thread = std::thread([this] { watch(); });
        }

        void stage(const std::string& description) {
            std::cout << "[demo-mod] " << description << "\n" << std::flush;
            std::lock_guard lock(mutex);
            currentStage = description;
            stageStart = std::chrono::steady_clock::now();
        }

        void stop() {
            {
                std::lock_guard lock(mutex);
                running = false;
            }
            wakeUp.notify_all();
            if (thread.joinable()) {
                thread.join();
            }
        }

    private:
        void watch() {
            std::unique_lock lock(mutex);
            while (running) {
                wakeUp.wait_for(lock, reportInterval, [this] { return !running; });
                if (!running || currentStage.empty()) {
                    continue;
                }
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::minutes>(std::chrono::steady_clock::now() - stageStart);
                if (elapsed < stageLimit) {
                    std::cout << "[demo-mod] Still working (" << elapsed.count() << " min): " << currentStage << "\n"
                              << std::flush;
                    continue;
                }
                std::cerr << "[fail] Stopping: this stage has run for " << elapsed.count()
                          << " minutes: " << currentStage << "\n"
                          << std::flush;
                abort();
            }
        }

        [[noreturn]] void abort() {
#ifdef _WIN32
            // Blender, Deploy and anything else this process started.
            const DWORD self = GetCurrentProcessId();
            const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W entry{};
                entry.dwSize = sizeof(entry);
                std::vector<DWORD> children;
                if (Process32FirstW(snapshot, &entry)) {
                    do {
                        if (entry.th32ParentProcessID == self) {
                            children.push_back(entry.th32ProcessID);
                        }
                    } while (Process32NextW(snapshot, &entry));
                }
                CloseHandle(snapshot);
                for (const DWORD child : children) {
                    terminateProcessTree(child);
                }
            }
#endif
            try {
                if (isHitmanRunning(hitmanDirectory)) {
                    closeHitmanGame(hitmanDirectory);
                }
            } catch (const std::exception& error) {
                std::cerr << "[fail] Could not close HITMAN: " << error.what() << "\n";
            }
            if (!modsIniContent.empty()) {
                std::ofstream output(hitmanDirectory / "Retail" / "mods.ini", std::ios::binary | std::ios::trunc);
                output << modsIniContent;
            }
            std::cerr << "[fail] The demo mod run was stopped. Rerun it to regenerate the unfinished missions.\n"
                      << std::flush;
            std::cout << std::flush;
            std::_Exit(2);
        }

        std::filesystem::path hitmanDirectory;
        std::string modsIniContent;
        std::mutex mutex;
        std::condition_variable wakeUp;
        std::thread thread;
        bool running = false;
        std::string currentStage;
        std::chrono::steady_clock::time_point stageStart = std::chrono::steady_clock::now();
    };

    Watchdog watchdog;

    // ------------------------------------------------------------------------------------------------ helpers

    std::string jsonEscape(const std::string_view value) {
        std::string escaped;
        for (const char c : value) {
            switch (c) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    escaped += buffer;
                } else {
                    escaped += c;
                }
            }
        }
        return escaped;
    }

    // ------------------------------------------------------------------------------------------------ arguments

    bool parseBool(const std::string_view option, const std::string_view value) {
        require(value == "true" || value == "false", std::string(option) + " only accepts true or false.");
        return value == "true";
    }

    Arguments resolveArguments(const int argc, char** argv, const std::filesystem::path& executablePath) {
        Arguments arguments;
        const ConfiguredPaths configuredPaths = readConfiguredPaths(
            executablePath, std::filesystem::u8path(NAVKIT_TEST_SOURCE_DIR) / "src" / "resource" / "NavKit.ini");
        std::string hitman = configuredPaths.hitman;
        std::string blender = configuredPaths.blender;
        // Work files (scenes, ALOCs, GLBs) default to the build folder rather than NavKit.ini's output folder.
        std::string output = NAVKIT_DEMO_MOD_WORK_DIR;
        std::string mod = NAVKIT_DEMO_MOD_DIR;
        std::string missions;

        for (int index = 1; index < argc; ++index) {
            std::string_view argument(argv[index]);
            std::string value;
            const size_t equals = argument.find('=');
            const bool hasInlineValue = argument.starts_with("--") && equals != std::string_view::npos;
            if (hasInlineValue) {
                value = argument.substr(equals + 1);
                argument = argument.substr(0, equals);
            }
            const auto nextValue = [&]() -> std::string {
                if (hasInlineValue) {
                    return value;
                }
                require(index + 1 < argc, std::string(argument) + " requires a value.");
                return argv[++index];
            };
            if (argument == "--hitman") {
                hitman = nextValue();
            } else if (argument == "--blender") {
                blender = nextValue();
            } else if (argument == "--output") {
                output = nextValue();
            } else if (argument == "--mod") {
                mod = nextValue();
            } else if (argument == "--missions") {
                missions = nextValue();
            } else if (argument == "--mission") {
                arguments.missionFilters.push_back(toLower(nextValue()));
            } else if (argument == "--exclude") {
                arguments.excludeFilters.push_back(toLower(nextValue()));
            } else if (argument == "--missing") {
                arguments.onlyMissing = parseBool(argument, nextValue());
            } else if (argument == "--crmd") {
                arguments.crmd = parseBool(argument, nextValue());
            } else if (argument == "--keepscenefiles") {
                arguments.keepSceneFiles = parseBool(argument, nextValue());
            } else if (argument == "--deploy") {
                arguments.deploy = parseBool(argument, nextValue());
            } else if (argument == "--smoketest") {
                arguments.smokeTest = parseBool(argument, nextValue());
            } else if (argument == "--generate") {
                arguments.generate = parseBool(argument, nextValue());
            } else if (argument == "--dryrun") {
                arguments.dryRun = parseBool(argument, nextValue());
            } else if (argument == "--discover") {
                arguments.discover = parseBool(argument, nextValue());
            } else if (argument == "--gameversion") {
                arguments.gameVersion = nextValue();
            } else if (argument == "--interactivemode") {
                arguments.interactiveMode = parseBool(argument, nextValue());
            } else if (argument == "--help" || argument == "-h") {
                std::cout
                    << "Usage: NavKitDemoModGenerator [--mod <NavKitDemoMod folder>] [--missions <missions json>]\n"
                       "    [--mission <name or scene substring>]... [--exclude <name or scene substring>]...\n"
                       "    [--missing true|false] [--hitman <HITMAN 3 folder>] [--blender <exe>]\n"
                       "    [--output <work folder>] [--crmd true|false] [--deploy true|false]\n"
                       "    [--smoketest true|false] [--generate true|false] [--dryrun true|false]\n"
                       "    [--keepscenefiles true|false]\n"
                       "    [--gameversion steam]\n"
                       "    [--interactivemode true|false]\n"
                       "--dryrun true only lists each mission's official NAVP, AIRG and CRMD resources.\n"
                       "--generate false deploys, verifies and smoke tests the files earlier runs generated.\n"
                       "--keepscenefiles true also saves each mission's .nav.json, .glb and .blend to the mod's\n"
                       "    navkit-source folder.\n"
                       "--missing true only takes missions the mod has no generated files for.\n"
                       "--exclude <name or scene substring> skips matching missions.\n"
                       "--discover true lists every mission scene with a NAVP or AIRG and whether the missions\n"
                       "    file has it.\n";
                std::exit(0);
            } else {
                throw std::runtime_error("Unknown argument '" + std::string(argument) + "'. Use --help for usage.");
            }
        }

        require(arguments.gameVersion == "steam", "Unsupported game version '" + arguments.gameVersion + "'.");
        require(!hitman.empty(), "No Hitman installation path is configured in NavKit.ini or on the command line.");
        require(!blender.empty(), "No Blender executable path is configured in NavKit.ini or on the command line.");
        require(!output.empty(), "No output directory is configured in NavKit.ini or on the command line.");
        arguments.hitmanDirectory = std::filesystem::u8path(hitman);
        arguments.blenderExecutable = std::filesystem::u8path(blender);
        arguments.outputDirectory = std::filesystem::u8path(output);
        arguments.modDirectory = mod.empty()
            ? arguments.hitmanDirectory / "Simple Mod Framework" / "Mods" / "NavKitDemoMod"
            : std::filesystem::u8path(mod);
        arguments.missionsFile =
            missions.empty() ? arguments.modDirectory / missionsFileName : std::filesystem::u8path(missions);
        require(std::filesystem::is_regular_file(arguments.modDirectory / "manifest.json"),
            "The demo mod folder " + arguments.modDirectory.string() + " has no manifest.json.");
        // The smoke test loads what SMF deployed, so it is only meaningful after a deploy.
        arguments.smokeTest = arguments.smokeTest && arguments.deploy;
        return arguments;
    }

    std::vector<Mission> loadMissions(const Arguments& arguments) {
        simdjson::ondemand::parser parser;
        const simdjson::padded_string json = simdjson::padded_string::load(arguments.missionsFile.string());
        simdjson::ondemand::document document = parser.iterate(json);
        std::vector<Mission> missions;
        for (simdjson::ondemand::object missionJson : document["missions"].get_array()) {
            Mission mission;
            mission.name = std::string(std::string_view(missionJson["name"].get_string()));
            mission.scene = std::string(std::string_view(missionJson["scene"].get_string()));
            require(mission.scene.starts_with("assembly:/") && mission.scene.ends_with(".entity"),
                "Mission '" + mission.name + "' has an invalid scene path '" + mission.scene + "'.");
            missions.push_back(std::move(mission));
        }
        require(!missions.empty(), "The missions file " + arguments.missionsFile.string() + " lists no missions.");
        const auto matches = [](const Mission& mission, const std::vector<std::string>& filters) {
            const std::string name = toLower(mission.name);
            const std::string scene = toLower(mission.scene);
            return std::ranges::any_of(filters,
                [&](const std::string& filter) { return name == filter || scene.find(filter) != std::string::npos; });
        };
        std::erase_if(missions, [&](const Mission& mission) { return matches(mission, arguments.excludeFilters); });
        if (arguments.missionFilters.empty()) {
            return missions;
        }
        std::vector<Mission> selected;
        for (const Mission& mission : missions) {
            if (matches(mission, arguments.missionFilters)) {
                selected.push_back(mission);
            }
        }
        require(!selected.empty(), "No mission in the missions file matches the --mission filters.");
        return selected;
    }

    // ------------------------------------------------------------------------------------------------ manifest

    using GeneratedFilesByMission = std::map<std::string, std::vector<GeneratedFile>>;

    /// Files the generator wrote on earlier runs, by mission name, with paths relative to the mod folder.
    GeneratedFilesByMission loadGeneratedFiles(const std::filesystem::path& modDirectory) {
        GeneratedFilesByMission generatedFiles;
        const std::filesystem::path manifestPath = modDirectory / generatedManifestFileName;
        if (!std::filesystem::is_regular_file(manifestPath)) {
            return generatedFiles;
        }
        simdjson::ondemand::parser parser;
        const simdjson::padded_string json = simdjson::padded_string::load(manifestPath.string());
        simdjson::ondemand::document document = parser.iterate(json);
        for (simdjson::ondemand::object missionJson : document["missions"].get_array()) {
            const std::string name(std::string_view(missionJson["name"].get_string()));
            std::vector<GeneratedFile>& files = generatedFiles[name];
            for (simdjson::ondemand::object fileJson : missionJson["files"].get_array()) {
                GeneratedFile file;
                file.relativePath = std::filesystem::u8path(std::string_view(fileJson["path"].get_string()));
                std::string_view value;
                if (fileJson["hash"].get_string().get(value) == simdjson::SUCCESS) {
                    file.hash = value;
                }
                if (fileJson["type"].get_string().get(value) == simdjson::SUCCESS) {
                    file.type = value;
                }
                if (fileJson["crowd"].get_string().get(value) == simdjson::SUCCESS) {
                    file.crowd = value;
                }
                files.push_back(std::move(file));
            }
        }
        return generatedFiles;
    }

    void removeGeneratedFiles(const std::filesystem::path& modDirectory, const std::vector<GeneratedFile>& files) {
        const std::filesystem::path contentDirectory = (modDirectory / "content").lexically_normal();
        std::set<std::filesystem::path> directories;
        for (const GeneratedFile& file : files) {
            const std::filesystem::path path = (modDirectory / file.relativePath).lexically_normal();
            const auto mismatch =
                std::mismatch(contentDirectory.begin(), contentDirectory.end(), path.begin(), path.end());
            require(mismatch.first == contentDirectory.end() && mismatch.second != path.end(),
                "Refusing to delete a generated file outside the mod's content folder: " + path.string());
            std::filesystem::remove(path);
            directories.insert(path.parent_path());
        }
        for (const std::filesystem::path& directory : directories) {
            if (std::filesystem::is_directory(directory) && std::filesystem::is_empty(directory)) {
                std::filesystem::remove(directory);
            }
        }
    }

    std::string currentTimestamp() {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &now);
#else
        gmtime_r(&now, &utc);
#endif
        std::ostringstream stream;
        stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return stream.str();
    }

    /// Rewrites the manifest: missions generated in this run replace their earlier entries; others are kept.
    void writeManifest(const std::filesystem::path& modDirectory, const std::vector<Mission>& allMissions,
        const std::vector<MissionResult>& results, const GeneratedFilesByMission& previous) {
        std::map<std::string, const MissionResult*> resultsByName;
        for (const MissionResult& result : results) {
            resultsByName[result.mission.name] = &result;
        }
        std::ostringstream json;
        json << "{\n  \"navkitVersion\": \"" << NavKit_VERSION_MAJOR << "." << NavKit_VERSION_MINOR << "."
             << NavKit_VERSION_PATCH << "\",\n  \"generated\": \"" << currentTimestamp() << "\",\n  \"missions\": [";
        bool firstMission = true;
        for (const Mission& mission : allMissions) {
            const auto resultIt = resultsByName.find(mission.name);
            const auto previousIt = previous.find(mission.name);
            const bool generated = resultIt != resultsByName.end() && resultIt->second->error.empty();
            if (!generated && previousIt == previous.end()) {
                continue;
            }
            json << (firstMission ? "" : ",") << "\n    {\n      \"name\": \"" << jsonEscape(mission.name)
                 << "\",\n      \"scene\": \"" << jsonEscape(mission.scene) << "\",\n      \"files\": [";
            firstMission = false;
            bool firstFile = true;
            for (const GeneratedFile& file : generated ? resultIt->second->files : previousIt->second) {
                json << (firstFile ? "" : ",") << "\n        {\"path\": \""
                     << jsonEscape(file.relativePath.generic_string()) << "\", \"hash\": \"" << file.hash
                     << "\", \"type\": \"" << file.type << "\"";
                if (!file.crowd.empty()) {
                    json << ", \"crowd\": \"" << jsonEscape(file.crowd) << "\"";
                }
                json << "}";
                firstFile = false;
            }
            json << "\n      ]\n    }";
        }
        json << "\n  ]\n}\n";
        std::ofstream output(modDirectory / generatedManifestFileName, std::ios::binary | std::ios::trunc);
        require(output.is_open(), "Could not write the generated-files manifest.");
        output << json.str();
    }

    GenerationOptions generationOptions(const Arguments& arguments) {
        GenerationOptions options;
        options.hitmanDirectory = arguments.hitmanDirectory;
        options.blenderExecutable = arguments.blenderExecutable;
        options.contentDirectory = arguments.modDirectory / "content";
        options.gameVersion = arguments.gameVersion;
        options.types = {true, true, arguments.crmd};
        options.keepSceneFiles = arguments.keepSceneFiles;
        return options;
    }

    // ------------------------------------------------------------------------------------------------ deploy & test

    void deployMod(const Arguments& arguments, const std::filesystem::path& runRoot) {
        watchdog.stage("Deploying mods with Simple Mod Framework...");
        deployWithSmf(
            arguments.hitmanDirectory, arguments.hitmanDirectory / "Simple Mod Framework", runRoot / "deploy.log");
    }

    /// Loads the mission with the deployed mod, waits until the scene can be listed through the editor, and checks
    /// HITMAN is still running a little later.
    void smokeTestMission(const Arguments& arguments, const Mission& mission, const std::filesystem::path& runRoot) {
        watchdog.stage("Smoke testing " + mission.name + " with the deployed mod...");
        const std::filesystem::path smokeDirectory = runRoot / slugify(mission.name) / "smoke-test";
        std::filesystem::create_directories(smokeDirectory);
        runMissionInGame(generationOptions(arguments), mission, smokeDirectory, true);
        std::cout << "[demo-mod] " << mission.name << " loaded and ran with the generated files.\n" << std::flush;
    }

    std::filesystem::path makeRunRoot(const std::filesystem::path& outputRoot) {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const std::filesystem::path runRoot = outputRoot / ("demo-mod-" + std::to_string(stamp));
        std::filesystem::create_directories(runRoot);
        return runRoot;
    }

    /// Lists every mission scene that has a NAVP or AIRG and whether the missions file has it, then the NAVPs and
    /// AIRGs in the game that none of these scenes use.
    int discoverMissions(const std::vector<Mission>& allMissions) {
        std::set<std::string> listedScenes;
        for (const Mission& mission : allMissions) {
            listedScenes.insert(toLower(mission.scene));
        }
        std::set<std::string> claimed;
        size_t missing = 0;
        std::cout << "\n[demo-mod] Mission scenes with a NAVP or AIRG:\n";
        for (const DiscoveredMission& discovered : NavKit::Bulk::discoverMissions(&claimed)) {
            const bool listed = listedScenes.contains(toLower(discovered.mission.scene));
            missing += listed ? 0 : 1;
            std::cout << (listed ? "  [listed]  " : "  [missing] ") << discovered.mission.scene;
            for (const SceneResource& resource : discovered.resources) {
                if (resource.type == "NAVP" || resource.type == "AIRG") {
                    std::cout << " | " << resource.type << " " << resource.hash << " (" << resource.chunk << ")";
                }
            }
            std::cout << '\n';
        }

        std::cout << "\n[demo-mod] NAVPs and AIRGs that none of these scenes use:\n";
        for (const char* type : {"NAVP", "AIRG"}) {
            RustStringList* hashes =
                get_all_resources_hashes_by_type_from_rpkg_files(Rpkg::partitionManager, type, Logger::rustLogCallback);
            require(hashes != nullptr, std::string("Could not list the game's ") + type + " resources.");
            for (uintptr_t i = 0; i < hashes->length; ++i) {
                const std::string hash = normalizeHash(get_string_from_list(hashes, i));
                if (!claimed.contains(hash)) {
                    const std::optional<ResourceInfo> info = getResourceInfo(hash);
                    std::cout << "  " << type << " " << hash << " (" << (info ? info->partition : "?") << ")\n";
                }
            }
            free_rust_string_list(hashes);
        }
        std::cout << "\n[demo-mod] " << missing << " mission scenes are not in the missions file.\n";
        return 0;
    }

    int run(const Arguments& arguments) {
        std::vector<Mission> allMissions;
        {
            Arguments everyMission = arguments;
            everyMission.missionFilters.clear();
            everyMission.excludeFilters.clear();
            allMissions = loadMissions(everyMission);
        }
        std::vector<Mission> missions = loadMissions(arguments);
        if (arguments.onlyMissing) {
            const GeneratedFilesByMission generated = loadGeneratedFiles(arguments.modDirectory);
            std::erase_if(missions, [&generated](const Mission& mission) { return generated.contains(mission.name); });
            require(!missions.empty(), "Every selected mission already has generated files in the mod.");
        }
        const std::filesystem::path runRoot = makeRunRoot(arguments.outputDirectory);
        std::cout << "[demo-mod] Mod: " << arguments.modDirectory.string() << '\n'
                  << "[demo-mod] Missions: " << missions.size() << " of " << allMissions.size() << '\n'
                  << "[demo-mod] Work folder: " << runRoot.string() << '\n';

        if (arguments.discover) {
            initRpkg(arguments.hitmanDirectory, arguments.blenderExecutable, runRoot);
            return discoverMissions(allMissions);
        }
        if (arguments.dryRun) {
            initRpkg(arguments.hitmanDirectory, arguments.blenderExecutable, runRoot);
            bool failed = false;
            for (const Mission& mission : missions) {
                std::cout << "\n[demo-mod] " << mission.name << " (" << mission.scene << ", template "
                          << sceneTempHash(mission.scene) << ")\n";
                try {
                    for (const SceneResource& resource : findSceneResources(sceneTempHash(mission.scene))) {
                        const std::string owner = namedScene(resource);
                        const bool ownedElsewhere = !owner.empty() && owner != toLower(mission.scene) &&
                            std::ranges::any_of(allMissions,
                                [&owner](const Mission& listed) { return toLower(listed.scene) == owner; });
                        std::cout << "[demo-mod]   " << resource.type << " " << resource.hash << " in "
                                  << resource.chunk << (ownedElsewhere ? " (belongs to " + owner + ")" : "") << '\n';
                    }
                } catch (const std::exception& error) {
                    failed = true;
                    std::cerr << "[fail] " << mission.name << ": " << error.what() << '\n';
                }
            }
            return failed ? 1 : 0;
        }

        std::vector<MissionResult> results;
        std::set<std::string> listedScenes;
        for (const Mission& listed : allMissions) {
            listedScenes.insert(toLower(listed.scene));
        }
        if (arguments.generate) {
            // Remove this run's missions' earlier output so a failed mission falls back to the official files.
            const GeneratedFilesByMission previous = loadGeneratedFiles(arguments.modDirectory);
            GeneratedFilesByMission kept = previous;
            for (const Mission& mission : missions) {
                if (const auto it = previous.find(mission.name); it != previous.end()) {
                    removeGeneratedFiles(arguments.modDirectory, it->second);
                    kept.erase(mission.name);
                }
                // Also clear the mission's own folders, in case an interrupted run wrote files it never recorded.
                clearMissionFolders(arguments.modDirectory / "content", mission);
            }
            writeManifest(arguments.modDirectory, allMissions, {}, kept);
            if (arguments.deploy) {
                // Load the scenes with the official files for these missions while extracting them.
                deployMod(arguments, runRoot);
            }

            initRpkg(arguments.hitmanDirectory, arguments.blenderExecutable, runRoot);
            for (const Mission& mission : missions) {
                MissionResult& result = results.emplace_back();
                result.mission = mission;
                try {
                    std::cout << "\n[demo-mod] ===== " << mission.name << " (" << mission.scene << ") =====\n"
                              << std::flush;
                    generateMission(generationOptions(arguments), mission, listedScenes,
                        runRoot / slugify(mission.name), result, [&mission](Phase, const std::string& description) {
                            watchdog.stage(mission.name + ": " + description + "...");
                        });
                } catch (const std::exception& error) {
                    result.error = error.what();
                    std::cerr << "[fail] " << mission.name << ": " << error.what() << '\n';
                    try {
                        ensureHitmanClosed(arguments.hitmanDirectory);
                    } catch (const std::exception& closeError) {
                        std::cerr << "[fail] Could not close HITMAN: " << closeError.what() << '\n';
                    }
                    removeGeneratedFiles(arguments.modDirectory, result.files);
                    result.files.clear();
                }
                // Record progress after each mission, so an interrupted run still knows which files it wrote.
                writeManifest(arguments.modDirectory, allMissions, results, kept);
            }
            writeManifest(arguments.modDirectory, allMissions, results, kept);
        } else {
            // Deploy, verify and smoke test what earlier runs generated, without regenerating it.
            const GeneratedFilesByMission previous = loadGeneratedFiles(arguments.modDirectory);
            for (const Mission& mission : missions) {
                MissionResult& result = results.emplace_back();
                result.mission = mission;
                if (const auto it = previous.find(mission.name); it != previous.end()) {
                    result.files = it->second;
                } else {
                    result.error = "The mod has no generated files for this mission.";
                }
            }
        }

        std::map<std::string, std::string> hashOwners;
        for (const MissionResult& result : results) {
            for (const GeneratedFile& file : result.files) {
                if (const auto [it, inserted] = hashOwners.try_emplace(file.hash, result.mission.name);
                    !inserted && it->second != result.mission.name) {
                    std::cerr << "[warn] " << file.type << " " << file.hash << " is used by both " << it->second
                              << " and " << result.mission.name << "; the deployed file depends on load order.\n";
                }
            }
        }
        bool failed = std::ranges::any_of(results, [](const MissionResult& result) { return !result.error.empty(); });
        if (arguments.deploy) {
            deployMod(arguments, runRoot);
            verifyDeployedFiles(generationOptions(arguments), results, runRoot);
            failed = std::ranges::any_of(results, [](const MissionResult& result) { return !result.error.empty(); });
        }
        if (arguments.smokeTest) {
            for (MissionResult& result : results) {
                if (!result.error.empty()) {
                    continue;
                }
                try {
                    smokeTestMission(arguments, result.mission, runRoot);
                    result.smokeTested = true;
                } catch (const std::exception& error) {
                    result.error = std::string("smoke test failed: ") + error.what();
                    failed = true;
                    std::cerr << "[fail] " << result.mission.name << ": " << result.error << '\n';
                }
            }
        }

        std::cout << "\n[demo-mod] Summary\n";
        for (const MissionResult& result : results) {
            std::cout << (result.error.empty() ? "[pass] " : "[fail] ") << result.mission.name;
            if (result.error.empty()) {
                std::map<std::string, size_t> counts;
                for (const GeneratedFile& file : result.files) {
                    ++counts[file.type];
                }
                std::cout << ": " << counts["NAVP"] << " NAVP, " << counts["AIRG"] << " AIRG, " << counts["CRMD"]
                          << " CRMD" << (result.smokeTested ? ", loaded in game" : "");
            } else {
                std::cout << ": " << result.error;
            }
            std::cout << '\n';
            for (const std::string& warning : result.warnings) {
                std::cout << "       warning: " << warning << '\n';
            }
        }
        std::cout << "[demo-mod] Work files are in " << runRoot.string() << '\n';
        return failed ? 1 : 0;
    }
} // namespace

int main(const int argc, char** argv) {
    configureConsoleWidth();
    int result = 0;
    bool interactiveMode = false;
    // NavKit's own messages, such as scene load and build errors, go to NavKit.log in the working directory.
    Logger::getInstance().start();
    setResourceDirectory(std::filesystem::u8path(NAVKIT_TEST_SOURCE_DIR) / "src" / "resource");
    wxInitializer wx;
    if (!wx) {
        std::cerr << "[fail] Could not initialize wxWidgets.\n";
        result = 1;
    } else {
        try {
            const std::filesystem::path executablePath =
                std::filesystem::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring());
            const Arguments arguments = resolveArguments(argc, argv, executablePath);
            interactiveMode = arguments.interactiveMode;
            std::cout << "[demo-mod] HITMAN: " << arguments.hitmanDirectory.string() << '\n'
                      << "[demo-mod] Blender: " << arguments.blenderExecutable.string() << '\n';
            if (!arguments.dryRun && !arguments.discover) {
                watchdog.start(arguments.hitmanDirectory);
            }
            result = run(arguments);
            watchdog.stop();
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
    Logger::getInstance().stop();
    return result;
}
