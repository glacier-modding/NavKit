#include "../include/NavKit/bulk/LivePipeline.h"
#include "../include/NavKit/module/NavKitSettings.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <conio.h>
#endif
#include <wx/init.h>
#include <wx/stdpaths.h>

namespace {
    using namespace NavKit::LivePipeline;

    struct IntegrationArguments {
        std::filesystem::path hitmanDirectory;
        std::filesystem::path blenderExecutable;
        std::filesystem::path outputDirectory;
        std::string gameVersion = "steam";
        bool interactiveMode = false;
    };

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
        const std::filesystem::path generatedGlb = outputDirectory / "output.glb";
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
            "-P", pathToWxString(scriptFile), "--", pathToWxString(sceneFile), pathToWxString(generatedGlb), "ALOC",
            "11111111", "copy", "true", "false"};
        std::vector<const wxChar*> blenderArgumentPointers;
        blenderArgumentPointers.reserve(blenderArguments.size() + 1);
        for (const wxString& argument : blenderArguments) {
            blenderArgumentPointers.push_back(argument.c_str());
        }
        blenderArgumentPointers.push_back(nullptr);
        require(wxExecute(blenderArgumentPointers.data(), wxEXEC_SYNC) == 0,
            "Blender failed while running the repository's Glacier2Glb.py script.");
        requireNonEmptyFile(generatedGlb, "Blender-generated GLB");

        RecastAdapter& adapter = RecastAdapter::getInstance();
        adapter.resetCommonSettings();
        adapter.cleanup();
        require(adapter.loadInputGeom(generatedGlb.string()), "Recast could not load the Blender-generated GLB.");
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

        const ConfiguredPaths configuredPaths = readConfiguredPaths(
            executablePath, std::filesystem::u8path(NAVKIT_TEST_SOURCE_DIR) / "src" / "resource" / "NavKit.ini");
        std::string hitman = configuredPaths.hitman;
        std::string blender = configuredPaths.blender;
        std::string output = configuredPaths.output;

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
        initRpkg(hitmanDirectory, blenderExecutable, outputRoot);

        ScopedAutoLoadScene autoLoadScene(hitmanDirectory / "Retail" / "mods.ini",
            "assembly:/_pro/scenes/missions/thefacility/_scene_mission_polarbear_intro_firsttime.entity");
        constexpr auto integrationTimeout = std::chrono::minutes(2);
        const Deadline deadline = std::chrono::steady_clock::now() + integrationTimeout;
        launchHitmanAndWaitForEditor(hitmanDirectory, gameVersion, deadline);

        const std::filesystem::path outputDirectory = makeRunDirectory(outputRoot);
        NavKitSettings::getInstance().outputFolder = outputDirectory.string();
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
    setResourceDirectory(std::filesystem::u8path(NAVKIT_TEST_SOURCE_DIR) / "src" / "resource");
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
