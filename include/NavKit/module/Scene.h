#pragma once
#include <functional>

class Scene {
public:
    explicit Scene();

    static const int SCENE_MENU_HEIGHT;

    ~Scene();

    static char *openLoadSceneFileDialog(const char *lastSceneFolder);

    static char *openSaveSceneFileDialog(char *lastSceneFolder);

    void setLastLoadFileName(char *file_name);

    void setLastSaveFileName(char *file_name);

    static void loadScene(std::string fileName, const std::function<void()> &callback,
                          const std::function<void()> &errorCallback);

    static void saveScene(char *fileName);

    void drawMenu();

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
    std::string lastSaveSceneFile;
};
