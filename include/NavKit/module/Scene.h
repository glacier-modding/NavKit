#pragma once
#include <functional>

class Scene {
public:
    explicit Scene();

    static const int SCENE_MENU_HEIGHT;

    ~Scene();

    static char *openLoadSceneFileDialog();

    static char *openSaveSceneFileDialog();

    void setLastLoadFileName(char *file_name);

    void setLastSaveFileName(char *file_name);

    void loadScene(const std::string &fileName, const std::function<void()> &callback,
                          const std::function<void()> &errorCallback);

    void saveScene(char *fileName);

    void handleOpenScenePressed();

    void handleSaveScenePressed();

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

private:
    int sceneScroll;
    std::string loadSceneName;
    std::string saveSceneName;
    static std::optional<std::jthread> backgroundWorker;
};
