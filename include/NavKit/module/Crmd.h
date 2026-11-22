#pragma once

#include <wx/dialog.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "../render/CoreProfileDraw.h"
#include "../util/CrmdDiff.h"
#include "../util/CrowdMapBuilder.h"

class SCrowdMapData;
class CrmdCompareDialog;

namespace Json {
    class Crowd;
}

class Crmd {
public:
    enum class CompareView { loaded, comparison, differences };

    explicit Crmd();

    ~Crmd() = default;

    static Crmd& getInstance() {
        static Crmd instance;
        return instance;
    }

    static std::unique_ptr<SCrowdMapData> readCrowdMapFile(const std::string& fileName);

    void renderCrowdMapData();
    void loadCrmdFromFile(const std::string& fileName);
    void loadCrowdMapData(const std::string& fileName);
    void loadComparisonCrmdFromFile(const std::string& fileName);
    void loadComparisonCrowdMapData(const std::string& fileName);
    void handleOpenComparisonCrmdClicked();
    void showExtractComparisonCrmdDialog();
    void clearComparison();
    bool canLoadComparison() const;
    bool canCompare() const;
    void setCompareView(CompareView view);
    void flipCompareView();
    void markDiffDirty();
    bool refreshDiff();
    void focusOnDiffRegion(size_t regionIndex) const;
    void showCompareDialog();
    void updateCompareDialog();
    void handleOpenCrmdClicked();
    void handleSaveCrmdClicked();
    void saveCrmd(const std::string& fileName) const;
    bool canSave() const;
    void setLastLoadFileName(const char* fileName);
    void clear();
    static void updateCrmdDialogControls(wxDialog* dialog);
    void showExtractCrmdDialog();
    static void extractCrmdFromRpkgs(const std::string& hash, bool asComparison = false);
    bool canBuildCrmd() const;
    void handleBuildCrmdClicked();
    void setCrowdMap(const CrowdMapBuilder::CrowdMap& map, const std::string& name);
    void loadSettings();
    void saveSettings() const;
    void showCrmdSettingsDialog();

    std::optional<std::jthread> backgroundWorker;
    SCrowdMapData* crowdMapData{};
    SCrowdMapData* comparisonCrowdMapData{};
    bool crmdLoaded;
    bool comparisonLoaded;
    bool loading;
    std::atomic<bool> saving;
    std::atomic<bool> building;
    bool showCrmd;
    CompareView compareView;
    std::string loadedCrmdText;
    std::string loadedComparisonText;
    CrmdDiff::Result diffResult;
    std::vector<CrmdDiff::Region> diffRegions;
    uint64_t diffGeneration = 0;
    static std::string selectedRpkgCrmd;
    static std::map<std::string, std::string> crmdHashIoiStringMap;
    static std::mutex crmdHashIoiStringMapMutex;
    static wxDialog* hCrmdDialog;
    static wxDialog* hCrmdSettingsDialog;
    static CrmdCompareDialog* hCompareDialog;
    bool snapHeightsToGlb;

private:
    struct RenderLayer {
        CoreProfileVertexBuffer cellTriangleBuffer;
        CoreProfileVertexBuffer flowArrowLineBuffer;
        glm::mat4 model{1.0f};
        std::atomic<bool> dirty{true};

        void draw() const;
    };

    void loadInto(const std::string& fileName, bool asComparison);
    void showExtractDialog(bool asComparison);
    static void rebuildRenderGeometry(const SCrowdMapData& data, RenderLayer& layer);
    void rebuildDiffGeometry();
    void saveCrmdAndReport(const std::string& fileName);
    void buildCrmd(const Json::Crowd& crowd, const CrowdMapBuilder::SceneVolumes& sceneVolumes,
        const std::vector<glm::dvec3>& navmeshTriangles, const std::vector<glm::dvec3>& glbTriangles);
    static char* openLoadCrmdFileDialog();
    static char* openSaveCrmdFileDialog();
    std::string loadCrmdName;
    std::string lastLoadCrmdFile;
    RenderLayer loadedLayer;
    RenderLayer comparisonLayer;
    RenderLayer diffLayer;
    std::atomic<bool> diffDirty{true};
};
