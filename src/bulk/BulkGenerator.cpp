#include "../../include/NavKit/bulk/BulkGenerator.h"

#include "../../include/NavKit/bulk/LivePipeline.h"
#include "../../include/NavKit/model/Json.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/QuickDigest/quickdigest5.hpp"
#include "../../include/navkit-rpkg-lib/navkit-rpkg-lib.h"

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <ranges>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <glm/glm.hpp>
#include <simdjson.h>

namespace NavKit::Bulk {
    using namespace NavKit::LivePipeline;

    namespace {
        /// Collects one message with << and logs it when it goes out of scope.
        class LogStream {
        public:
            explicit LogStream(const LogLevel level) : level(level) {}
            LogStream(const LogStream&) = delete;
            LogStream& operator=(const LogStream&) = delete;

            ~LogStream() {
                if (const std::string message = stream.str(); !message.empty()) {
                    log(level, message);
                }
            }

            template <typename T> LogStream& operator<<(const T& value) {
                stream << value;
                return *this;
            }

        private:
            LogLevel level;
            std::ostringstream stream;
        };

        LogStream info() {
            return LogStream(LogLevel::Info);
        }

        LogStream detail() {
            return LogStream(LogLevel::Detail);
        }

        /// "chunk19patch3.rpkg" -> "chunk19".
        std::string baseChunk(const std::string& partition) {
            static const std::regex chunkPattern(R"(^(chunk\d+))", std::regex::icase);
            std::smatch match;
            const std::string fileName = std::filesystem::path(partition).filename().string();
            require(std::regex_search(fileName, match, chunkPattern),
                "Could not determine the chunk of RPKG partition '" + partition + "'.");
            return toLower(match[1].str());
        }

        /// The runtime resource ID of an IOI path: the low 56 bits of the first 8 bytes of its lower-case MD5.
        std::string ioiPathHash(const std::string& ioiPath) {
            const std::vector<uint8_t> digest = QuickDigest5::digestString(toLower(ioiPath));
            std::ostringstream hash;
            hash << "00" << std::hex << std::uppercase << std::setfill('0');
            for (size_t i = 1; i < 8; ++i) {
                hash << std::setw(2) << static_cast<int>(digest[i]);
            }
            return hash.str();
        }

        /// "<hash>.<TYPE>" -> IOI path, for the resources in NavKit's hash list.
        const std::map<std::string, std::string>& hashListNames() {
            static const std::map<std::string, std::string> names = [] {
                std::map<std::string, std::string> result;
                const std::filesystem::path hashListPath = resourceFile("hash_list_filtered.txt");
                std::ifstream hashList(hashListPath);
                require(hashList.is_open(), "Could not read " + hashListPath.string() + ".");
                for (std::string line; std::getline(hashList, line);) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    if (const size_t comma = line.find(','); comma != std::string::npos) {
                        result[toUpper(line.substr(0, comma))] = line.substr(comma + 1);
                    }
                }
                return result;
            }();
            return names;
        }

        struct VanillaCrmd {
            const SceneResource* resource;
            Crmd::Summary summary;
        };

        /// Picks the official CRMD a generated crowd map replaces. Candidates are the CRMDs referenced by the template
        /// that holds the crowd; among them the one with the same grid size, then the nearest grid origin, wins. When
        /// the crowd's template is unknown, a CRMD only matches with the same grid size and an origin within a metre.
        const SceneResource* matchCrmd(const Json::Crowd& crowd, const Crmd::Summary& generated,
            const std::vector<VanillaCrmd>& vanillaCrmds, const std::set<std::string>& usedHashes) {
            const std::string crowdTblu = crowd.tblu.empty() ? std::string{} : normalizeHash(crowd.tblu);
            const auto sameGrid = [&generated](const VanillaCrmd& candidate) {
                return candidate.summary.cellsX == generated.cellsX && candidate.summary.cellsY == generated.cellsY;
            };
            const auto distance = [&generated](const VanillaCrmd& candidate) {
                return glm::distance(candidate.summary.origin, generated.origin);
            };
            const VanillaCrmd* best = nullptr;
            for (const VanillaCrmd& candidate : vanillaCrmds) {
                if (usedHashes.contains(candidate.resource->hash) || crowdTblu.empty() ||
                    !candidate.resource->ownerTblus.contains(crowdTblu)) {
                    continue;
                }
                if (best == nullptr || (sameGrid(candidate) && !sameGrid(*best)) ||
                    (sameGrid(candidate) == sameGrid(*best) && distance(candidate) < distance(*best))) {
                    best = &candidate;
                }
            }
            if (best != nullptr) {
                return best->resource;
            }
            for (const VanillaCrmd& candidate : vanillaCrmds) {
                if (!usedHashes.contains(candidate.resource->hash) && sameGrid(candidate) &&
                    distance(candidate) < 1.0f && (best == nullptr || distance(candidate) < distance(*best))) {
                    best = &candidate;
                }
            }
            return best == nullptr ? nullptr : best->resource;
        }

        void writeMissionFile(const GenerationOptions& options, const Mission& mission, const SceneResource& resource,
            const std::filesystem::path& source, MissionResult& result, const std::string& crowd = {}) {
            const std::filesystem::path destination = options.contentDirectory / resource.chunk /
                slugify(mission.name) / (resource.hash + "." + resource.type);
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing);
            const std::filesystem::path relativePath =
                destination.lexically_relative(options.contentDirectory.parent_path());
            result.files.push_back({resource.hash, resource.type, relativePath, crowd});
            info() << "Wrote " << relativePath.generic_string();
        }

        void generateCrmds(const GenerationOptions& options, const Mission& mission,
            const std::vector<SceneResource>& resources, const std::map<std::string, std::string>& crmdOwners,
            const std::filesystem::path& workDirectory, MissionResult& result, const PhaseCallback& onPhase) {
            const std::vector<Json::Crowd> crowds = Scene::getInstance().crowds;
            std::vector<const SceneResource*> crmdResources;
            for (const SceneResource& resource : resources) {
                if (resource.type == "CRMD") {
                    crmdResources.push_back(&resource);
                }
            }
            if (crowds.empty() && crmdResources.empty()) {
                info() << "The scene has no crowds.";
                return;
            }
            if (crmdResources.empty()) {
                result.warnings.push_back("The scene has " + std::to_string(crowds.size()) +
                    " crowds but references no CRMD; skipped CRMDs.");
                return;
            }

            Crmd& crmd = Crmd::getInstance();
            std::vector<VanillaCrmd> vanillaCrmds;
            for (const SceneResource* resource : crmdResources) {
                const std::filesystem::path vanillaFile =
                    extractResource(options.hitmanDirectory, resource->hash, "CRMD", workDirectory / "vanilla");
                crmd.clear();
                crmd.loadCrowdMapData(vanillaFile.string());
                const std::optional<Crmd::Summary> summary = crmd.summary();
                require(summary.has_value(), "Could not load the official CRMD " + resource->hash + ".");
                vanillaCrmds.push_back({resource, *summary});
            }

            std::set<std::string> usedHashes;
            for (size_t index = 0; index < crowds.size(); ++index) {
                const Json::Crowd& crowd = crowds[index];
                if (onPhase) {
                    onPhase(Phase::BuildingCrmd, "Building the CRMD for crowd " + crowd.name);
                }
                crmd.clear();
                const CrowdMapBuilder::Report report = crmd.buildCrmdForCrowd(crowd);
                const std::filesystem::path crmdFile = workDirectory / ("crowd_" + std::to_string(index) + ".crmd");
                crmd.saveCrmd(crmdFile.string());
                requireNonEmptyFile(crmdFile, "Serialized CRMD");

                crmd.clear();
                crmd.loadCrowdMapData(crmdFile.string());
                const std::optional<Crmd::Summary> generated = crmd.summary();
                // A bad crowd map keeps the crowd's official CRMD rather than dropping the mission's NAVP and AIRG.
                if (!generated.has_value()) {
                    result.warnings.push_back("The CRMD built for crowd " + crowd.name +
                        " could not be reloaded; the crowd keeps its official CRMD.");
                    continue;
                }
                if (generated->walkableCells == 0) {
                    result.warnings.push_back("The CRMD built for crowd " + crowd.name +
                        " has no walkable cells; the crowd keeps its official CRMD.");
                    continue;
                }
                if (report.walkableCells != generated->walkableCells) {
                    result.warnings.push_back("The reloaded CRMD for crowd " + crowd.name + " has " +
                        std::to_string(generated->walkableCells) + " walkable cells but the built crowd map had " +
                        std::to_string(report.walkableCells) + "; the crowd keeps its official CRMD.");
                    continue;
                }

                const SceneResource* target = matchCrmd(crowd, *generated, vanillaCrmds, usedHashes);
                if (target == nullptr) {
                    result.warnings.push_back(
                        "Could not match crowd " + crowd.name + " to an official CRMD; it was not written.");
                    continue;
                }
                usedHashes.insert(target->hash);
                if (const auto owner = crmdOwners.find(target->hash); owner != crmdOwners.end()) {
                    result.warnings.push_back("CRMD " + target->hash + " for crowd " + crowd.name + " belongs to " +
                        owner->second + "; this mission uses that mission's generated file.");
                    continue;
                }
                const auto vanilla = std::ranges::find_if(
                    vanillaCrmds, [target](const VanillaCrmd& candidate) { return candidate.resource == target; });
                info() << "Crowd " << crowd.name << ": generated " << generated->cellsX << "x" << generated->cellsY
                       << " cells, " << generated->walkableCells << " walkable; official " << target->hash << " has "
                       << vanilla->summary.cellsX << "x" << vanilla->summary.cellsY << " cells, "
                       << vanilla->summary.walkableCells << " walkable.";
                writeMissionFile(options, mission, *target, crmdFile, result, crowd.name);
            }
            for (const SceneResource* resource : crmdResources) {
                if (!usedHashes.contains(resource->hash) && !crmdOwners.contains(resource->hash)) {
                    result.warnings.push_back("No crowd in the scene matched the official CRMD " + resource->hash +
                        "; it keeps the official file.");
                }
            }
        }

        const std::map<std::string, std::string> locationNames = {{"ancestral", "Dartmoor"}, {"bangkok", "Bangkok"},
            {"caged", "Siberia (sniper)"}, {"coastaltown", "Sapienza"}, {"colombia", "Santa Fortuna"},
            {"colorado", "Colorado"}, {"colorado_2", "Colorado"}, {"edgy", "Berlin"}, {"elegant", "Mendoza"},
            {"golden", "Dubai"}, {"greedy", "New York"}, {"hawk", "Himmelstein (sniper)"}, {"hokkaido", "Hokkaido"},
            {"marrakesh", "Marrakesh"}, {"miami", "Miami"}, {"mumbai", "Mumbai"}, {"opulent", "Haven Island"},
            {"paris", "Paris"}, {"rocky", "Ambrose Island"}, {"salty", "Hantu Port (sniper)"}, {"sheep", "Hawke's Bay"},
            {"skunk", "Whittleton Creek"}, {"snug", "Safehouse"}, {"theark", "Isle of Sgail"},
            {"thefacility", "ICA Facility"}, {"trapped", "Carpathian Mountains"}, {"wet", "Chongqing"}};

        /// Story missions, by scene, with the names the NavKit Demo Mod uses.
        const std::map<std::string, std::string> storyMissionNames = {
            {"thefacility/_scene_mission_polarbear_intro_firsttime", "Arrival"},
            {"thefacility/_scene_mission_polarbear_module_002", "Guided Training"},
            {"thefacility/_scene_mission_polarbear_module_002_b", "Freeform Training"},
            {"thefacility/_scene_mission_polarbear_module_005", "The Final Test"},
            {"paris/_scene_fashionshowhit_01", "The Showstopper"}, {"coastaltown/mission01", "World of Tomorrow"},
            {"marrakesh/_scene_mission_spider", "A Gilded Cage"}, {"bangkok/_scene_mission_tiger", "Club 27"},
            {"colorado_2/_scene_mission_bull", "Freedom Fighters"},
            {"hokkaido/_scene_mission_snowcrane", "Situs Inversus"}, {"sheep/scene_sheep", "Nightcall"},
            {"miami/scene_flamingo", "The Finish Line"}, {"colombia/scene_hippo", "Three-Headed Serpent"},
            {"mumbai/scene_mongoose", "Chasing a Ghost"}, {"skunk/scene_skunk", "Another Life"},
            {"theark/scene_magpie", "The Ark Society"},
            {"greedy/mission_raccoon/scene_raccoon_basic", "Golden Handshake"},
            {"opulent/mission_stingray/scene_stingray_basic", "The Last Resort"},
            {"golden/mission_gecko/scene_gecko_basic", "On Top of the World"},
            {"ancestral/scene_bulldog", "Death in the Family"}, {"edgy/mission_fox/scene_fox_basic", "Apex Predator"},
            {"elegant/scene_llama", "The Farewell"}};

        /// Scenes whose NAVP and AIRG are named after another scene, so they cannot be found from the hash list.
        const std::array extraMissionScenes{
            "assembly:/_pro/scenes/missions/thefacility/_scene_mission_polarbear_intro_firsttime.entity"};
    } // namespace

    std::string toLower(std::string value) {
        std::ranges::transform(value, value.begin(), [](const unsigned char c) { return std::tolower(c); });
        return value;
    }

    std::string toUpper(std::string value) {
        std::ranges::transform(value, value.begin(), [](const unsigned char c) { return std::toupper(c); });
        return value;
    }

    std::string slugify(const std::string& value) {
        std::string slug;
        for (const unsigned char c : value) {
            if (std::isalnum(c)) {
                slug += static_cast<char>(std::tolower(c));
            } else if (!slug.empty() && slug.back() != '-') {
                slug += '-';
            }
        }
        while (!slug.empty() && slug.back() == '-') {
            slug.pop_back();
        }
        return slug.empty() ? "mission" : slug;
    }

    std::string normalizeHash(std::string hash) {
        hash = toUpper(std::move(hash));
        if (hash.size() < 16) {
            hash.insert(0, 16 - hash.size(), '0');
        }
        return hash;
    }

    std::string sceneTempHash(const std::string& scenePath) {
        return ioiPathHash("[" + scenePath + "].pc_entitytemplate");
    }

    std::vector<char> readBinaryFile(const std::filesystem::path& path) {
        std::ifstream stream(path, std::ios::binary);
        require(stream.is_open(), "Could not open " + path.string() + ".");
        return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    }

    std::optional<ResourceInfo> getResourceInfo(const std::string& hash) {
        char* jsonText = get_resource_info_json(hash.c_str(), Rpkg::partitionManager, Logger::rustLogCallback);
        if (jsonText == nullptr) {
            return std::nullopt;
        }
        const simdjson::padded_string json(std::string_view{jsonText});
        free_string(jsonText);
        simdjson::ondemand::parser parser;
        simdjson::ondemand::document document = parser.iterate(json);
        ResourceInfo info;
        info.hash = std::string(std::string_view(document["hash"].get_string()));
        info.type = std::string(std::string_view(document["type"].get_string()));
        info.partition = std::string(std::string_view(document["partition"].get_string()));
        for (simdjson::ondemand::object reference : document["references"].get_array()) {
            info.references.push_back({std::string(std::string_view(reference["hash"].get_string())),
                std::string(std::string_view(reference["type"].get_string()))});
        }
        return info;
    }

    std::vector<SceneResource> findSceneResources(const std::string& sceneHash) {
        std::map<std::string, SceneResource> resources;
        std::set<std::string> visited{sceneHash};
        std::deque<std::string> pending{sceneHash};
        size_t templateCount = 0;
        while (!pending.empty()) {
            const std::string hash = pending.front();
            pending.pop_front();
            const std::optional<ResourceInfo> info = getResourceInfo(hash);
            if (!info) {
                require(hash != sceneHash, "The scene's entity template " + hash + " is not in the game's RPKGs.");
                continue;
            }
            ++templateCount;
            std::set<std::string> templateTblus;
            for (const ResourceReference& reference : info->references) {
                if (reference.type == "TBLU") {
                    templateTblus.insert(normalizeHash(reference.hash));
                }
            }
            for (const ResourceReference& reference : info->references) {
                if (reference.type == "TEMP") {
                    if (visited.insert(reference.hash).second) {
                        pending.push_back(reference.hash);
                    }
                } else if (reference.type == "NAVP" || reference.type == "AIRG" || reference.type == "CRMD") {
                    const std::string resourceHash = normalizeHash(reference.hash);
                    auto [it, inserted] = resources.try_emplace(resourceHash);
                    SceneResource& resource = it->second;
                    if (inserted) {
                        const std::optional<ResourceInfo> resourceInfo = getResourceInfo(reference.hash);
                        require(resourceInfo.has_value(),
                            "Could not find " + reference.type + " " + resourceHash + ", referenced by template " +
                                hash + ", in the game's RPKGs.");
                        resource.hash = resourceHash;
                        resource.type = reference.type;
                        resource.chunk = baseChunk(resourceInfo->partition);
                    }
                    if (reference.type == "CRMD") {
                        resource.ownerTblus.insert(templateTblus.begin(), templateTblus.end());
                    }
                }
            }
        }
        detail() << "Walked " << templateCount << " entity templates of the scene.";
        std::vector<SceneResource> result;
        for (auto& resource : resources | std::views::values) {
            result.push_back(std::move(resource));
        }
        return result;
    }

    std::string namedScene(const SceneResource& resource) {
        std::string path;
        if (const auto it = hashListNames().find(resource.hash + "." + resource.type); it != hashListNames().end()) {
            path = it->second;
        }
        const size_t end = path.find(']');
        if (path.empty() || path.front() != '[' || end == std::string::npos) {
            return {};
        }
        std::string scene = path.substr(1, end - 1);
        if (const size_t extension = scene.rfind('.'); extension != std::string::npos) {
            scene.resize(extension);
        }
        if (resource.type == "AIRG" && scene.ends_with("_scene")) {
            scene.resize(scene.size() - std::string_view("_scene").size());
        }
        return toLower(scene + ".entity");
    }

    std::vector<DiscoveredMission> discoverMissions(
        std::set<std::string>* claimed, const std::function<void(size_t done, size_t total)>& progress) {
        static const std::regex resourcePath(
            R"(^\[(assembly:/_pro/scenes/missions/.+)\.(navp|airg)\]\.pc_(navp|airg)$)", std::regex::icase);
        std::set<std::string> candidates(extraMissionScenes.begin(), extraMissionScenes.end());
        for (const auto& [key, path] : hashListNames()) {
            const bool airg = key.ends_with(".AIRG");
            if (!airg && !key.ends_with(".NAVP")) {
                continue;
            }
            std::smatch match;
            if (!std::regex_match(path, match, resourcePath)) {
                continue;
            }
            std::string scene = match[1].str();
            if (airg && scene.ends_with("_scene")) {
                scene.resize(scene.size() - std::string_view("_scene").size());
            }
            candidates.insert(scene + ".entity");
        }

        std::vector<DiscoveredMission> missions;
        size_t done = 0;
        for (const std::string& scene : candidates) {
            if (progress) {
                progress(done++, candidates.size());
            }
            if (!getResourceInfo(sceneTempHash(scene)).has_value()) {
                continue;
            }
            std::vector<SceneResource> resources = findSceneResources(sceneTempHash(scene));
            const bool hasNavigation = std::ranges::any_of(resources,
                [](const SceneResource& resource) { return resource.type == "NAVP" || resource.type == "AIRG"; });
            if (!hasNavigation) {
                continue;
            }
            if (claimed != nullptr) {
                for (const SceneResource& resource : resources) {
                    if (resource.type == "NAVP" || resource.type == "AIRG") {
                        claimed->insert(resource.hash);
                    }
                }
            }
            missions.push_back({{missionDisplayName(scene), scene}, std::move(resources)});
        }
        if (progress) {
            progress(candidates.size(), candidates.size());
        }
        return missions;
    }

    std::string missionDisplayName(const std::string& scenePath) {
        static const std::string prefix = "assembly:/_pro/scenes/missions/";
        std::string relative = toLower(scenePath);
        if (relative.starts_with(prefix)) {
            relative.erase(0, prefix.size());
        }
        if (relative.ends_with(".entity")) {
            relative.resize(relative.size() - std::string_view(".entity").size());
        }
        if (const auto it = storyMissionNames.find(relative); it != storyMissionNames.end()) {
            return it->second;
        }
        const std::string folder = relative.substr(0, relative.find('/'));
        const std::string scene = relative.substr(relative.rfind('/') + 1);
        const auto location = locationNames.find(folder);
        return (location != locationNames.end() ? location->second : folder) + ": " + scene;
    }

    void resetNavKitState() {
        Scene::getInstance().clear();
        Airg::getInstance().clear();
        Crmd::getInstance().clear();
    }

    void ensureHitmanClosed(const std::filesystem::path& hitmanDirectory) {
        if (isHitmanRunning(hitmanDirectory)) {
            closeHitmanGame(hitmanDirectory);
            // Give Steam a moment to register the exit before the next -applaunch.
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    std::filesystem::path extractResource(const std::filesystem::path& hitmanDirectory, const std::string& hash,
        const std::string& type, const std::filesystem::path& folder) {
        std::filesystem::create_directories(folder);
        const char* hashes[] = {hash.c_str()};
        extract_resources_from_rpkg((hitmanDirectory / "Runtime").string().c_str(), hashes, 1, Rpkg::partitionManager,
            folder.string().c_str(), type.c_str(), Logger::rustLogCallback);
        const std::filesystem::path extracted = folder / (hash + "." + type);
        requireNonEmptyFile(extracted, "Extracted " + type + " " + hash);
        return extracted;
    }

    void runMissionInGame(const GenerationOptions& options, const Mission& mission,
        const std::filesystem::path& directory, const bool smokeTest) {
        const std::array settleTimes{options.sceneSettleTime, std::chrono::seconds(120)};
        for (size_t attempt = 0; attempt < settleTimes.size(); ++attempt) {
            ensureHitmanClosed(options.hitmanDirectory);
            resetNavKitState();
            ScopedAutoLoadScene autoLoadScene(options.hitmanDirectory / "Retail" / "mods.ini", mission.scene);
            const Deadline deadline = std::chrono::steady_clock::now() + std::chrono::minutes(4) + settleTimes[attempt];
            try {
                launchHitmanAndWaitForEditor(options.hitmanDirectory, options.gameVersion, deadline);
                extractSceneFromRunningGame(directory, deadline, settleTimes[attempt]);
                if (smokeTest) {
                    std::this_thread::sleep_for(std::chrono::seconds(20));
                    require(isHitmanRunning(options.hitmanDirectory),
                        "HITMAN exited while running the mission with the generated files.");
                }
                closeHitmanGame(options.hitmanDirectory);
                autoLoadScene.restore();
                return;
            } catch (const std::exception& error) {
                ensureHitmanClosed(options.hitmanDirectory);
                if (attempt + 1 == settleTimes.size()) {
                    throw;
                }
                log(LogLevel::Warning,
                    mission.name + ": " + error.what() + " Retrying in a new game, waiting " +
                        std::to_string(settleTimes[attempt + 1].count()) + " seconds before requesting the scene.");
            }
        }
    }

    std::filesystem::path defaultSourceDirectory(const std::filesystem::path& contentDirectory) {
        return contentDirectory.lexically_normal().parent_path() / "navkit-source";
    }

    std::filesystem::path sceneFilesDirectory(const std::filesystem::path& sourceDirectory, const Mission& mission) {
        return sourceDirectory / slugify(mission.name);
    }

    void clearMissionFolders(const std::filesystem::path& contentDirectory, const Mission& mission) {
        if (!std::filesystem::is_directory(contentDirectory)) {
            return;
        }
        for (const auto& chunk : std::filesystem::directory_iterator(contentDirectory)) {
            if (const std::filesystem::path missionFolder = chunk.path() / slugify(mission.name);
                chunk.is_directory() && std::filesystem::is_directory(missionFolder)) {
                std::filesystem::remove_all(missionFolder);
            }
        }
    }

    void generateMission(const GenerationOptions& options, const Mission& mission,
        const std::set<std::string>& listedScenes, const std::filesystem::path& workDirectory, MissionResult& result,
        const PhaseCallback& onPhase) {
        const auto phase = [&onPhase](const Phase value, const std::string& description) {
            if (onPhase) {
                onPhase(value, description);
            }
        };
        require(options.types.navp || options.types.airg || options.types.crmd, "No file type is selected.");
        std::filesystem::create_directories(workDirectory);
        NavKitSettings::getInstance().outputFolder = workDirectory.string();
        resetNavKitState();

        result.mission = mission;
        result.sceneHash = sceneTempHash(mission.scene);
        phase(Phase::FindingResources,
            "Finding the official NAVP, AIRG and CRMD resources (scene template " + result.sceneHash + ")");
        const std::vector<SceneResource> resources = findSceneResources(result.sceneHash);
        std::vector<const SceneResource*> navps;
        std::vector<const SceneResource*> airgs;
        for (const SceneResource& resource : resources) {
            detail() << "  " << resource.type << " " << resource.hash << " in " << resource.chunk;
            if (resource.type == "NAVP") {
                navps.push_back(&resource);
            } else if (resource.type == "AIRG") {
                airgs.push_back(&resource);
            }
        }
        require(!navps.empty() || !airgs.empty(), "The scene references no NAVP or AIRG.");
        // A variant scene also references its base mission's NAVP or AIRG; that one is left to the base mission.
        std::set<std::string> baseScenes;
        const auto keepOwn = [&](std::vector<const SceneResource*>& candidates) {
            std::erase_if(candidates, [&](const SceneResource* resource) {
                const std::string owner = namedScene(*resource);
                if (owner.empty() || owner == toLower(mission.scene) || !listedScenes.contains(owner)) {
                    return false;
                }
                baseScenes.insert(owner);
                result.warnings.push_back(resource->type + " " + resource->hash + " belongs to " + owner +
                    "; this mission uses that mission's generated file.");
                return true;
            });
        };
        keepOwn(navps);
        keepOwn(airgs);
        // So are the base mission's crowd maps, which the variant's crowds share.
        std::map<std::string, std::string> crmdOwners;
        if (options.types.crmd) {
            for (const std::string& baseScene : baseScenes) {
                try {
                    for (const SceneResource& baseResource : findSceneResources(sceneTempHash(baseScene))) {
                        if (baseResource.type == "CRMD") {
                            crmdOwners.emplace(baseResource.hash, baseScene);
                        }
                    }
                } catch (const std::exception& error) {
                    log(LogLevel::Warning, "Could not read the resources of " + baseScene + ": " + error.what());
                }
            }
        }
        if (!options.types.navp) {
            navps.clear();
        }
        if (!options.types.airg) {
            airgs.clear();
        }
        require(!navps.empty() || !airgs.empty() || options.types.crmd,
            "Every selected NAVP and AIRG the scene uses belongs to another mission.");
        if (navps.size() > 1 || airgs.size() > 1) {
            result.warnings.push_back("The scene references " + std::to_string(navps.size()) + " NAVPs and " +
                std::to_string(airgs.size()) + " AIRGs of its own; all of them are replaced with the generated files.");
        }

        phase(Phase::LoadingScene, "Loading the scene in HITMAN and extracting it");
        runMissionInGame(options, mission, workDirectory, false);
        phase(Phase::BuildingGlb,
            "Extracted " + std::to_string(Scene::getInstance().meshes.size()) + " meshes and " +
                std::to_string(Scene::getInstance().crowds.size()) + " crowds; building the GLB with Blender");
        extractAssetsAndBuildGlb(
            options.hitmanDirectory, options.blenderExecutable, workDirectory, options.keepSceneFiles);
        // AIRG and CRMD generation both read the NAVP, so it is built even when it is not written.
        phase(Phase::BuildingNavp, "Building the NAVP");
        const std::filesystem::path navpFile = buildNavp(workDirectory);
        std::filesystem::path airgFile;
        if (!airgs.empty()) {
            phase(Phase::BuildingAirg, "Building the AIRG");
            airgFile = buildAirg(workDirectory);
        }

        phase(Phase::Saving, "Saving the generated files");
        for (const SceneResource* navp : navps) {
            writeMissionFile(options, mission, *navp, navpFile, result);
        }
        for (const SceneResource* airg : airgs) {
            writeMissionFile(options, mission, *airg, airgFile, result);
        }
        if (options.types.crmd) {
            generateCrmds(options, mission, resources, crmdOwners, workDirectory, result, onPhase);
        }
        if (options.keepSceneFiles) {
            // The generated game files are already saved, so a problem here only costs the kept copies.
            try {
                phase(Phase::Saving, "Building the .blend and keeping the .nav.json, .glb and .blend");
                buildBlendFile(options.blenderExecutable, workDirectory);
                const std::filesystem::path keptDirectory = sceneFilesDirectory(options.sourceDirectory.empty()
                        ? defaultSourceDirectory(options.contentDirectory)
                        : options.sourceDirectory,
                    mission);
                std::filesystem::create_directories(keptDirectory);
                const std::string keptName = slugify(mission.name);
                for (const auto& [source, extension] :
                    {std::pair{"output.nav.json", ".nav.json"}, {"output.glb", ".glb"}, {"output.blend", ".blend"}}) {
                    std::filesystem::copy_file(workDirectory / source, keptDirectory / (keptName + extension),
                        std::filesystem::copy_options::overwrite_existing);
                }
                info() << "Kept the .nav.json, .glb and .blend in " << keptDirectory.string();
            } catch (const std::exception& error) {
                result.warnings.push_back(
                    std::string("Could not keep the .nav.json, .glb and .blend: ") + error.what());
            }
        }
    }

    std::optional<std::filesystem::path> smfDirectoryForContent(const std::filesystem::path& contentDirectory) {
        const std::filesystem::path content = contentDirectory.lexically_normal();
        const std::filesystem::path mods = content.parent_path().parent_path();
        if (toLower(content.filename().string()) != "content" || toLower(mods.filename().string()) != "mods") {
            return std::nullopt;
        }
        const std::filesystem::path smfDirectory = mods.parent_path();
        if (!std::filesystem::is_regular_file(smfDirectory / "Deploy.exe")) {
            return std::nullopt;
        }
        return smfDirectory;
    }

    void deployWithSmf(const std::filesystem::path& hitmanDirectory, const std::filesystem::path& smfDirectory,
        const std::filesystem::path& logFile) {
        const std::filesystem::path deployExecutable = smfDirectory / "Deploy.exe";
        require(std::filesystem::is_regular_file(deployExecutable),
            "Could not find Simple Mod Framework's Deploy.exe at " + deployExecutable.string() + ".");
        ensureHitmanClosed(hitmanDirectory);
        // The scanned packages keep the RPKGs open, which stops SMF from writing its patch RPKGs.
        if (Rpkg::partitionManager != nullptr) {
            free_partition_manager(Rpkg::partitionManager);
            Rpkg::partitionManager = nullptr;
            Rpkg::extractionDataInitComplete = false;
        }
#ifdef _WIN32
        std::filesystem::create_directories(logFile.parent_path());
        constexpr int attempts = 2;
        for (int attempt = 1; attempt <= attempts; ++attempt) {
            info() << "Deploying mods with Simple Mod Framework (log: " << logFile.string() << ")";
            unsigned long exitCode = 0;
            if (runLoggedCommand(deployExecutable, {}, smfDirectory, logFile, std::chrono::minutes(15), exitCode) ==
                CommandResult::Finished) {
                require(exitCode == 0,
                    "Simple Mod Framework's Deploy.exe failed with exit code " + std::to_string(exitCode) + "; see " +
                        logFile.string() + ".");
                info() << "Deploy finished.";
                return;
            }
            log(LogLevel::Warning, "Deploy.exe did not finish within 15 minutes and was stopped.");
        }
        throw std::runtime_error("Simple Mod Framework's Deploy.exe did not finish; see " + logFile.string() + ".");
#else
        (void)logFile;
        throw std::runtime_error("Deploying with Simple Mod Framework is only supported on Windows.");
#endif
    }

    void verifyDeployedFiles(const GenerationOptions& options, std::vector<MissionResult>& results,
        const std::filesystem::path& workDirectory) {
        initRpkg(options.hitmanDirectory, options.blenderExecutable, workDirectory);
        const std::filesystem::path root = options.contentDirectory.parent_path();
        const std::filesystem::path verifyDirectory = workDirectory / "deployed";
        std::map<std::string, std::vector<std::pair<std::string, std::filesystem::path>>> filesByHash;
        for (const MissionResult& result : results) {
            if (result.error.empty()) {
                for (const GeneratedFile& file : result.files) {
                    filesByHash[file.hash].emplace_back(result.mission.name, root / file.relativePath);
                }
            }
        }
        size_t verified = 0;
        for (MissionResult& result : results) {
            if (!result.error.empty()) {
                continue;
            }
            for (const GeneratedFile& file : result.files) {
                const std::optional<ResourceInfo> resourceInfo = getResourceInfo(file.hash);
                if (!resourceInfo.has_value()) {
                    result.error = "The deployed RPKGs do not contain " + file.type + " " + file.hash + ".";
                    break;
                }
                const std::vector<char> deployed =
                    readBinaryFile(extractResource(options.hitmanDirectory, file.hash, file.type, verifyDirectory));
                if (deployed == readBinaryFile(root / file.relativePath)) {
                    detail() << "Deployed " << file.type << " " << file.hash << " matches (" << resourceInfo->partition
                             << ").";
                    ++verified;
                    continue;
                }
                const auto& owners = filesByHash[file.hash];
                const auto deployedOwner = std::ranges::find_if(
                    owners, [&deployed](const auto& owner) { return readBinaryFile(owner.second) == deployed; });
                if (deployedOwner != owners.end()) {
                    result.warnings.push_back(file.type + " " + file.hash + " is shared with " + deployedOwner->first +
                        ", whose generated file is the one deployed.");
                    ++verified;
                    continue;
                }
                result.error = "The deployed " + file.type + " " + file.hash + " (from " + resourceInfo->partition +
                    ") is not the generated file.";
                log(LogLevel::Error, result.mission.name + ": " + result.error);
                break;
            }
        }
        info() << "Verified " << verified << " deployed resources.";
    }
} // namespace NavKit::Bulk
