#pragma once
#include "../model/Nav.h"

class Scene {
public:
    explicit Scene();

    ~Scene();

    char * openLoadSceneFileDialog(std::string::pointer data);
    char * openSaveSceneFileDialog(std::string::pointer data);

    void setLastLoadFileName(char * file_name);

    void setLastSaveFileName(char * file_name);

    void drawMenu();

    static Scene &getInstance() {
        static Scene instance;
        return instance;
    }

    Nav nav;

private:
    int sceneScroll;
    bool sceneLoaded;
    std::string loadSceneName;
    std::string lastLoadSceneFile;
    std::string saveSceneName;
    std::string lastSaveSceneFile;
};
