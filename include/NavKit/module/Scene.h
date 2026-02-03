#pragma once
#include <functional>
#define WIN32_LEAN_AND_MEAN
#include <map>
#include <windows.h>
#include "../model/Json.h"

class Scene {
public:
    explicit Scene();

    ~Scene();

    static char *openLoadSceneFileDialog();

    static char *openSaveSceneFileDialog();

    void setLastLoadFileName(char *file_name);

    void setLastSaveFileName(char *file_name);

    void saveScene(char *fileName) const;

    void handleOpenSceneClicked();

    void handleSaveSceneClicked();

    void loadScene(const std::string &fileName, const std::function<void()> &callback,
                   const std::function<void()> &errorCallback);

    void loadVersion(
        simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void loadMeshes(const std::function<void()> &errorCallback,
                    simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void loadPfBoxes(const std::function<void()> &errorCallback,
                    simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void loadPfSeedPoints(const std::function<void()> &errorCallback,
                    simdjson::simdjson_result<simdjson::ondemand::document> &jsonDocument);

    void loadMatis(const std::function<void()>& errorCallback,
                     simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument);

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

    std::vector<Json::Mesh> meshes;
    std::vector<Json::Mesh> prims;
    Json::PfBox includeBox;
    std::vector<Json::PfBox> exclusionBoxes;
    std::vector<Json::PfSeedPoint> pfSeedPoints;
    std::map<std::string, Json::Mati> matis;
    std::optional<std::jthread> backgroundWorker;
    float bBoxPos[3]{};
    float bBoxScale[3]{};
    bool showBBox;
    int version;
    static HWND hSceneDialog;

    const Json::Mesh *findMeshByHashAndIdAndPos(const std::string &hash, const std::string &id, const float *pos) const;

private:
    std::string loadSceneName;
    std::string saveSceneName;

    static INT_PTR CALLBACK sceneDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};
