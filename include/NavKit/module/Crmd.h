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
    bool renderGeometryDirty{true};
    CoreProfileVertexBuffer cellTriangleBuffer;
    CoreProfileVertexBuffer flowArrowLineBuffer;
    glm::mat4 renderModel{1.0f};
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
    void loadCrmdFromFiles(const std::vector<std::pair<std::string, std::string>>& filesAndNames);
    void loadCrowdMapData(const std::string& fileName, const std::string& name);
    void handleOpenCrmdClicked();
    void handleSaveCrmdClicked();
    void saveCrmd(const std::string& fileName, size_t index) const;
    bool canSave() const;
    void clear();
    static void updateCrmdDialogControls(wxDialog* dialog);
    void showExtractCrmdDialog();
    static void extractCrmdsFromRpkgs(const std::vector<std::pair<std::string, std::string>>& hashesAndNames);
    bool canBuildAllCrmds() const;
    void handleBuildAllCrmdsClicked();
    void setCrowdMap(const CrowdMapBuilder::CrowdMap& map, const std::string& name, const std::string& crowdId = {});
    void selectCrowdMap(int index);
    int getSelectedCrowdMapIndex() const;
    size_t crowdMapCount() const;
    std::vector<std::string> crowdMapNames() const;
    std::string selectedCrowdMapName() const;
    const SCrowdMapData* crowdMapData(size_t index) const;
    void loadSettings();
    void saveSettings() const;
    void showCrmdSettingsDialog();

    std::optional<std::jthread> backgroundWorker;
    std::atomic<bool> loading;
    std::atomic<bool> saving;
    std::atomic<bool> building;
    bool snapHeightsToGlb;
    static std::map<std::string, std::string> crmdHashIoiStringMap;
    static std::mutex crmdHashIoiStringMapMutex;
    static wxDialog* hCrmdDialog;
    static wxDialog* hCrmdSettingsDialog;

private:
    static void rebuildRenderGeometry(LoadedCrowdMap& crowdMap);
    void addCrowdMap(std::unique_ptr<SCrowdMapData> data, const std::string& name, const std::string& sourceKey);
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
    int selectedCrowdMapIndex{-1};
};
