#pragma once

#include <wx/dialog.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>
#include <optional>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "../render/CoreProfileDraw.h"

class SCrowdMapData;

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

    std::optional<std::jthread> backgroundWorker;
    SCrowdMapData* crowdMapData{};
    bool crmdLoaded;
    bool loading;
    std::atomic<bool> saving;
    bool showCrmd;
    std::string loadedCrmdText;
    static std::string selectedRpkgCrmd;
    static std::map<std::string, std::string> crmdHashIoiStringMap;
    static std::mutex crmdHashIoiStringMapMutex;
    static wxDialog* hCrmdDialog;

private:
    void rebuildRenderGeometry();
    void saveCrmdAndReport(const std::string& fileName);
    static char* openLoadCrmdFileDialog();
    static char* openSaveCrmdFileDialog();
    std::string loadCrmdName;
    std::string lastLoadCrmdFile;
    std::atomic<bool> renderGeometryDirty{true};
    CoreProfileVertexBuffer cellTriangleBuffer;
    CoreProfileVertexBuffer flowArrowLineBuffer;
    glm::mat4 renderModel{1.0f};
};
