#pragma once
#include <functional>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../model/ZPathfinding.h"

class Scene {
public:
    explicit Scene();

    ~Scene();

    static char *openLoadSceneFileDialog();

    static char *openSaveSceneFileDialog();

    void setLastLoadFileName(char *file_name);

    void setLastSaveFileName(char *file_name);

    void loadScene(const std::string &fileName, const std::function<void()> &callback,
                   const std::function<void()> &errorCallback);

    void saveScene(char *fileName) const;

    void handleOpenSceneClicked();

    void handleSaveSceneClicked();

    void loadMeshes(const std::function<void()> &errorCallback,
                   simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void loadPfBoxes(const std::function<void()> &errorCallback,
                     simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void loadVersion(
        simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void loadPfSeedPoints(const std::function<void()> &errorCallback,
                          simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void showSceneDialog();

    void setBBox(const float *pos, const float *scale);

    void resetBBoxDefaults();

    void updateSceneDialogControls(HWND hDlg) const;

    static Scene &getInstance() {
        static Scene instance;
        return instance;
    }

    std::string lastLoadSceneFile;
    bool sceneLoaded;

    std::vector<ZPathfinding::Mesh> meshes;
    std::vector<ZPathfinding::Mesh> prims;
    ZPathfinding::PfBox includeBox;
    std::vector<ZPathfinding::PfBox> exclusionBoxes;
    std::vector<ZPathfinding::PfSeedPoint> pfSeedPoints;
    std::optional<std::jthread> backgroundWorker;
    float bBoxPos[3]{};
    float bBoxScale[3]{};
    bool showBBox;
    int version;
    static HWND hSceneDialog;

    const ZPathfinding::Mesh *findMeshByHashAndIdAndPos(const std::string &hash, const std::string &id, const float *pos) const;

private:
    std::string loadSceneName;
    std::string saveSceneName;

    static INT_PTR CALLBACK sceneDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};
