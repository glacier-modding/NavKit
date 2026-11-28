#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

/// Generates NavKit files for vanilla missions and writes them under the official resource hashes, in the layout
/// Simple Mod Framework expects: <content folder>/<chunk>/<mission>/<hash>.<type>. Shared by the NavKit Bulk Generation
/// Tool and the NavKit Demo Mod generator.
namespace NavKit::Bulk {
    struct Mission {
        std::string name;
        std::string scene;
    };

    /// A NAVP, AIRG or CRMD a scene uses, and the base chunk SMF must patch to replace it.
    struct SceneResource {
        std::string hash;
        std::string type;
        std::string chunk;
        std::set<std::string> ownerTblus;
    };

    struct GeneratedFile {
        std::string hash;
        std::string type;
        /// Relative to the content folder's parent, for example content/chunk0/arrival/00F1BA373CABEEE1.NAVP.
        std::filesystem::path relativePath;
        std::string crowd;
    };

    struct MissionResult {
        Mission mission;
        std::string sceneHash;
        std::vector<GeneratedFile> files;
        std::vector<std::string> warnings;
        std::string error;
        bool smokeTested = false;
    };

    struct FileTypes {
        bool navp = true;
        bool airg = true;
        bool crmd = true;
    };

    struct GenerationOptions {
        std::filesystem::path hitmanDirectory;
        std::filesystem::path blenderExecutable;
        /// The folder files are written under, as <chunk>/<mission>/<hash>.<type>.
        std::filesystem::path contentDirectory;
        std::string gameVersion = "steam";
        FileTypes types;
        /// Also saves each mission's .nav.json, .glb and .blend under sourceDirectory.
        bool keepSceneFiles = false;
        /// The folder kept source files are written under, as <mission>/<mission>.<extension>. Empty means
        /// defaultSourceDirectory(contentDirectory).
        std::filesystem::path sourceDirectory;
        /// Requesting the scene while a mission is still loading crashed HITMAN on Mendoza.
        std::chrono::seconds sceneSettleTime{45};
    };

    enum class Phase { FindingResources, LoadingScene, BuildingGlb, BuildingNavp, BuildingAirg, BuildingCrmd, Saving };

    /// Called when generating a mission moves to another phase, with a description of it.
    using PhaseCallback = std::function<void(Phase phase, const std::string& description)>;

    std::string toLower(std::string value);
    std::string toUpper(std::string value);
    std::string slugify(const std::string& value);
    std::string normalizeHash(std::string hash);
    /// The runtime resource ID of a scene's entity template.
    std::string sceneTempHash(const std::string& scenePath);
    std::vector<char> readBinaryFile(const std::filesystem::path& path);

    struct ResourceReference {
        std::string hash;
        std::string type;
    };

    struct ResourceInfo {
        std::string hash;
        std::string type;
        std::string partition;
        std::vector<ResourceReference> references;
    };

    /// The latest version of a resource in the scanned RPKGs; needs LivePipeline::initRpkg first.
    std::optional<ResourceInfo> getResourceInfo(const std::string& hash);

    /// Walks the scene's entity templates and returns the NAVPs, AIRGs and CRMDs they reference, with the TBLUs of the
    /// templates that reference each CRMD (a crowd's TBLU, as the scene JSON gives it).
    std::vector<SceneResource> findSceneResources(const std::string& sceneHash);

    /// The mission scene NavKit's hash list names a NAVP or AIRG after ("<scene>.navp" or "<scene>_scene.airg"), in
    /// lower case, or empty when it has no name.
    std::string namedScene(const SceneResource& resource);

    struct DiscoveredMission {
        Mission mission;
        std::vector<SceneResource> resources;
    };

    /// Every mission scene that uses a NAVP or AIRG, from the names in NavKit's hash list, checked against the scanned
    /// RPKGs. Fills @p claimed, when given, with the hashes of the NAVPs and AIRGs these scenes use.
    std::vector<DiscoveredMission> discoverMissions(
        std::set<std::string>* claimed = nullptr, const std::function<void(size_t done, size_t total)>& progress = {});

    /// "The Showstopper" for a story mission, otherwise "<location>: <scene>", as the demo mod names missions.
    std::string missionDisplayName(const std::string& scenePath);

    void resetNavKitState();
    void ensureHitmanClosed(const std::filesystem::path& hitmanDirectory);

    std::filesystem::path extractResource(const std::filesystem::path& hitmanDirectory, const std::string& hash,
        const std::string& type, const std::filesystem::path& folder);

    /// Launches HITMAN on the mission and extracts its scene into @p directory, then, for a smoke test, checks the
    /// game is still running 20 seconds later. A failed attempt is retried once in a fresh game with a longer wait.
    void runMissionInGame(const GenerationOptions& options, const Mission& mission,
        const std::filesystem::path& directory, bool smokeTest);

    /// <content folder's parent>/navkit-source. Source files stay out of the content folder because SMF would pack some
    /// of these file types into the RPKGs.
    std::filesystem::path defaultSourceDirectory(const std::filesystem::path& contentDirectory);

    /// Where a mission's kept .nav.json, .glb and .blend go: <source directory>/<mission>.
    std::filesystem::path sceneFilesDirectory(const std::filesystem::path& sourceDirectory, const Mission& mission);

    /// Deletes <content folder>/<any chunk>/<mission folder>, so a regenerated mission keeps no stale files.
    void clearMissionFolders(const std::filesystem::path& contentDirectory, const Mission& mission);

    /// Generates the selected file types for one mission into the content folder. A NAVP or AIRG named after another
    /// scene in @p listedScenes is left to that mission. Throws when the mission cannot be generated.
    void generateMission(const GenerationOptions& options, const Mission& mission,
        const std::set<std::string>& listedScenes, const std::filesystem::path& workDirectory, MissionResult& result,
        const PhaseCallback& onPhase = {});

    /// Simple Mod Framework's folder when @p contentDirectory is <SMF>/Mods/<mod>/content.
    std::optional<std::filesystem::path> smfDirectoryForContent(const std::filesystem::path& contentDirectory);

    /// Runs SMF's Deploy.exe, logging to @p logFile, after closing HITMAN and releasing the scanned RPKGs.
    void deployWithSmf(const std::filesystem::path& hitmanDirectory, const std::filesystem::path& smfDirectory,
        const std::filesystem::path& logFile);

    /// Rescans the RPKGs and checks the latest version of every generated resource is the generated file. A resource
    /// two missions share deploys one of their files; the other mission gets a warning. Any other mismatch fails that
    /// mission.
    void verifyDeployedFiles(const GenerationOptions& options, std::vector<MissionResult>& results,
        const std::filesystem::path& workDirectory);
} // namespace NavKit::Bulk
