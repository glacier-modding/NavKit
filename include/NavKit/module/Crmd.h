#pragma once

#include <wx/dialog.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include <string>
#include <optional>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "../model/CrmdSource.h"
#include "../render/CoreProfileDraw.h"
#include "../util/CrowdMapBuilder.h"

class SCrowdMapData;

namespace Json {
    class Crowd;
}

struct LoadedCrowdMap {
    ~LoadedCrowdMap();

    std::unique_ptr<SCrowdMapData> data;
    std::string name;
    std::string sourceKey;
    std::string crowdKey;
    FileSource source;
    bool renderGeometryDirty{true};
    bool visible{true};
    CoreProfileVertexBuffer cellTriangleBuffer;
    CoreProfileVertexBuffer flowArrowLineBuffer;
    glm::mat4 renderModel{1.0f};
};

struct CrmdFileToLoad {
    std::string fileName;
    std::string name;
    FileSource source;
    std::string crowdKey;
};

struct CrmdResourceToLoad {
    std::string hash;
    std::string name;
    std::string crowdKey;
};

class Crmd {
public:
    explicit Crmd();

    ~Crmd();

    static Crmd& getInstance() {
        static Crmd instance;
        return instance;
    }

    void renderCrowdMapData();
    bool loadCrmdFromFiles(const std::vector<CrmdFileToLoad>& files);
    void loadCrowdMapData(const std::string& fileName, const std::string& name, const FileSource& fileSource = {},
        const std::string& crowdKey = {});
    void handleOpenCrmdClicked();
    void handleSaveCrmdClicked();
    void saveCrmd(const std::string& fileName, size_t index) const;
    bool canSave() const;
    void clear();
    static void updateCrmdDialogControls(wxDialog* dialog);
    void showExtractCrmdDialog();
    static bool extractCrmdsFromRpkgs(const std::vector<CrmdResourceToLoad>& resources);
    bool canBuildAllCrmds() const;
    void handleBuildAllCrmdsClicked();
    static std::string crowdKey(const Json::Crowd& crowd);
    void setCrowdMap(const CrowdMapBuilder::CrowdMap& map, const std::string& name, const std::string& crowdKey = {});
    size_t crowdMapCount() const;
    std::vector<std::string> crowdMapNames() const;
    std::vector<bool> crowdMapVisibility() const;
    void setCrowdMapVisible(size_t index, bool visible);
    std::vector<CrmdSource> crowdMapSources() const;
    void setCrowdMapSource(size_t index, const FileSource& fileSource);
    const SCrowdMapData* crowdMapData(size_t index) const;
    void loadSettings();
    void saveSettings() const;
    void showCrmdSettingsDialog();
    static void updateCrmdSettingsDialogList();

    std::optional<std::jthread> backgroundWorker;
    std::atomic<bool> loading;
    std::atomic<bool> saving;
    std::atomic<bool> building;
    bool snapHeightsToGlb;
    bool showCrmd = true;
    static std::map<std::string, std::string> crmdHashIoiStringMap;
    static std::mutex crmdHashIoiStringMapMutex;
    static wxDialog* hCrmdDialog;
    static wxDialog* hCrmdSettingsDialog;

private:
    static void rebuildRenderGeometry(LoadedCrowdMap& crowdMap);
    void addCrowdMap(std::unique_ptr<SCrowdMapData> data, const std::string& name, const std::string& sourceKey,
        const std::string& crowdKey, const FileSource& fileSource);
    void saveCrmdAndReport(const std::string& fileName, size_t index);
    void buildCrmds(const std::vector<Json::Crowd>& crowds, const CrowdMapBuilder::SceneVolumes& sceneVolumes,
        const std::vector<glm::dvec3>& navmeshTriangles, const std::vector<std::vector<glm::dvec3>>& glbTriangles);
    void buildCrmd(const Json::Crowd& crowd, const CrowdMapBuilder::SceneVolumes& sceneVolumes,
        const std::vector<glm::dvec3>& navmeshTriangles, const std::vector<glm::dvec3>& glbTriangles);
    static std::vector<std::string> openLoadCrmdFilesDialog();
    static char* openSaveCrmdFileDialog(const std::string& defaultName);
    mutable std::mutex crowdMapsMutex;
    std::vector<std::unique_ptr<LoadedCrowdMap>> crowdMaps;
    std::vector<std::unique_ptr<LoadedCrowdMap>> crowdMapsAwaitingRelease;
};
