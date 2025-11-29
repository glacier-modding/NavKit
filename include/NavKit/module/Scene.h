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

    void saveScene(char *fileName) const;

    void handleOpenSceneClicked();

    void handleSaveSceneClicked();

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

private:
    int sceneScroll;
    std::string loadSceneName;
    std::string saveSceneName;
};
