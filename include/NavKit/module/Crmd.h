#pragma once

#include <wx/dialog.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
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

class Crmd {
public:
    explicit Crmd();

    ~Crmd() = default;

    static Crmd& getInstance() {
        static Crmd instance;
        return instance;
    }

    void renderCrowdMapData();
    void loadCrmdFromFile(const std::string& fileName);
    void loadCrowdMapData(const std::string& fileName);
    void handleOpenCrmdClicked();
    void handleSaveCrmdClicked();
    void saveCrmd(const std::string& fileName) const;
    bool canSave() const;
    void setLastLoadFileName(const char* fileName);
    void clear();
    static void updateCrmdDialogControls(wxDialog* dialog);
    void showExtractCrmdDialog();
    static void extractCrmdFromRpkgs(const std::string& hash);
    bool canBuildCrmd() const;
    void handleBuildCrmdClicked();
    void setCrowdMap(const CrowdMapBuilder::CrowdMap& map, const std::string& name);
    void loadSettings();
    void saveSettings() const;
    void showCrmdSettingsDialog();

    std::optional<std::jthread> backgroundWorker;
    SCrowdMapData* crowdMapData{};
    bool crmdLoaded;
    bool loading;
    std::atomic<bool> saving;
    std::atomic<bool> building;
    bool showCrmd;
    bool snapHeightsToGlb;
    std::string loadedCrmdText;
    static std::string selectedRpkgCrmd;
    static std::map<std::string, std::string> crmdHashIoiStringMap;
    static std::mutex crmdHashIoiStringMapMutex;
    static wxDialog* hCrmdDialog;
    static wxDialog* hCrmdSettingsDialog;

private:
    void rebuildRenderGeometry();
    void saveCrmdAndReport(const std::string& fileName);
    void buildCrmd(const Json::Crowd& crowd, const CrowdMapBuilder::SceneVolumes& sceneVolumes,
        const std::vector<glm::dvec3>& navmeshTriangles, const std::vector<glm::dvec3>& glbTriangles);
    static char* openLoadCrmdFileDialog();
    static char* openSaveCrmdFileDialog();
    std::string loadCrmdName;
    std::string lastLoadCrmdFile;
    std::atomic<bool> renderGeometryDirty{true};
    CoreProfileVertexBuffer cellTriangleBuffer;
    CoreProfileVertexBuffer flowArrowLineBuffer;
    glm::mat4 renderModel{1.0f};
};
