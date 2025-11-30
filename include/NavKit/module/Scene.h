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

    void showSceneDialog();
    void setBBox(const float* pos, const float* scale);
    void resetBBoxDefaults();

    static Scene &getInstance() {
        static Scene instance;
        return instance;
    }

    std::string lastLoadSceneFile;
    bool sceneLoaded;

    std::vector<ZPathfinding::Aloc> alocs;
    ZPathfinding::PfBox includeBox;
    std::vector<ZPathfinding::PfBox> exclusionBoxes;
    std::vector<ZPathfinding::PfSeedPoint> pfSeedPoints;
    std::optional<std::jthread> backgroundWorker;
    float bBoxPos[3]{};
    float bBoxScale[3]{};
    static HWND hSceneDialog;

private:
    int sceneScroll;
    std::string loadSceneName;
    std::string saveSceneName;
    static INT_PTR CALLBACK SceneDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};
