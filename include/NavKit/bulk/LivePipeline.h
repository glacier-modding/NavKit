#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/// The live-game steps shared by the end-to-end integration test and the demo mod generator: launching HITMAN on a
/// scene, extracting the scene through the editor connection, and building the GLB, NAVP and AIRG from it.
namespace NavKit::LivePipeline {
    using Deadline = std::chrono::steady_clock::time_point;

    /// Detail is step-by-step progress (socket calls, scan details); Info is the operations and their results.
    enum class LogLevel { Detail, Info, Warning, Error };
    using LogSink = std::function<void(LogLevel level, const std::string& message)>;

    /// Sends this library's messages to @p sink instead of stdout and stderr. The sink may be called from any thread.
    void setLogSink(LogSink sink);
    void log(LogLevel level, const std::string& message);

    /// Where Glacier2Glb.py, hash_list_filtered.txt and the other NavKit resources are read from; by default the
    /// executable's folder, as in an installed NavKit.
    void setResourceDirectory(const std::filesystem::path& directory);
    std::filesystem::path resourceFile(const std::string& name);

    void require(bool condition, std::string_view message);

    void configureConsoleWidth();

    void requireNonEmptyFile(const std::filesystem::path& path, std::string_view description);

    /// The Hitman, Blender and output paths from the repository's NavKit.ini, overridden by a NavKit.ini beside the
    /// executable when there is one.
    struct ConfiguredPaths {
        std::string hitman;
        std::string blender;
        std::string output;
    };

    ConfiguredPaths readConfiguredPaths(
        const std::filesystem::path& executablePath, const std::filesystem::path& defaultIniPath);

    /// Points the SDK's [sdk] auto_load_scene at @p scenePath until restore() or destruction.
    class ScopedAutoLoadScene {
    public:
        ScopedAutoLoadScene(const std::filesystem::path& modsIni, const std::string& scenePath);
        ~ScopedAutoLoadScene();
        ScopedAutoLoadScene(const ScopedAutoLoadScene&) = delete;
        ScopedAutoLoadScene& operator=(const ScopedAutoLoadScene&) = delete;

        void restore();

    private:
        std::filesystem::path modsIni;
        std::vector<std::string> originalSettings;
        bool restored = false;
    };

    /// Initializes the RPKG extraction data from the Hitman installation in NavKitSettings.
    void initRpkg(const std::filesystem::path& hitmanDirectory, const std::filesystem::path& blenderExecutable,
        const std::filesystem::path& outputDirectory);

    void launchHitmanAndWaitForEditor(
        const std::filesystem::path& hitmanDirectory, std::string_view gameVersion, Deadline deadline);

    /// Requests a normal shutdown of every HITMAN3.exe from @p hitmanDirectory and waits for each to exit.
    void closeHitmanGame(const std::filesystem::path& hitmanDirectory);

    bool isHitmanRunning(const std::filesystem::path& hitmanDirectory);

#ifdef _WIN32
    void terminateProcessTree(unsigned long processId);

    enum class CommandResult { Finished, TimedOut };

    /// Runs @p executable with @p arguments through cmd.exe without a console or any of this process's handles, with
    /// stdin from NUL and its output appended to @p logFile, and stops it after @p timeout. Started as a child that
    /// inherited the caller's handles, SMF's Deploy.exe waited forever on a child process that had already exited.
    CommandResult runLoggedCommand(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments,
        const std::filesystem::path& workingDirectory, const std::filesystem::path& logFile,
        std::chrono::minutes timeout, unsigned long& exitCode);
#endif

    /// Extracts the active scene into outputDirectory/output.nav.json and loads it, retrying until it has meshes.
    /// @p settleTime is waited before the first request and after a request that found no meshes: on some missions
    /// (Mendoza) asking for the scene while it is still loading crashes the game.
    void extractSceneFromRunningGame(const std::filesystem::path& outputDirectory, Deadline deadline,
        std::chrono::seconds settleTime = std::chrono::seconds(0));

    /// Extracts the scene's ALOCs, runs Glacier2Glb.py (output in outputDirectory/blender.log) and loads the glTF
    /// into Recast.
    /// With @p alsoWriteGlb, Blender also writes the same geometry to outputDirectory/output.glb.
    void extractAssetsAndBuildGlb(const std::filesystem::path& hitmanDirectory,
        const std::filesystem::path& blenderExecutable, const std::filesystem::path& outputDirectory,
        bool alsoWriteGlb = false);

    /// Writes outputDirectory/output.blend from the scene and ALOCs already there. This is a separate Blender run
    /// because a .blend build adds the scene's volumes, which must stay out of the geometry the NAVP is built from.
    std::filesystem::path buildBlendFile(
        const std::filesystem::path& blenderExecutable, const std::filesystem::path& outputDirectory);

    /// Builds, saves and reloads outputDirectory/output.navp; the reloaded NAVP stays loaded.
    std::filesystem::path buildNavp(const std::filesystem::path& outputDirectory);

    /// Builds, saves and validates outputDirectory/output.airg from the loaded NAVP.
    std::filesystem::path buildAirg(const std::filesystem::path& outputDirectory);
} // namespace NavKit::LivePipeline
