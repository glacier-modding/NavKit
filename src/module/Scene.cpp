#include <GL/glut.h>
#include "../../include/RecastDemo/imgui.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"

Scene::Scene() {
}

Scene::~Scene() {
}

char * Scene::openLoadSceneFileDialog(std::string::pointer data) {
    return 0;
}

char * Scene::openSaveSceneFileDialog(std::string::pointer data) {
    return 0;
}

void Scene::setLastLoadFileName(char *file_name) {
}

void Scene::setLastSaveFileName(char *file_name) {
}

void Scene::drawMenu() {
    Renderer &renderer = Renderer::getInstance();
    Gui &gui = Gui::getInstance();
    int sceneMenuHeight = 100;
    imguiBeginScrollArea("Scene menu", renderer.width - 250 - 10,
                         renderer.height - 10 - 205 - 417 - 238 - sceneMenuHeight - 10, 250, sceneMenuHeight, &sceneScroll);
        gui.mouseOverMenu = true;
    imguiLabel("Load NavKit Scene from file");
    if (imguiButton(loadSceneName.c_str())) {
        char *fileName = openLoadSceneFileDialog(lastLoadSceneFile.data());
        if (fileName) {
            setLastLoadFileName(fileName);
            std::string extension = loadSceneName.substr(loadSceneName.length() - 4, loadSceneName.length());
            std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

            sceneLoaded = true;
            std::string msg = "Loading nav.json file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
        }
    }
    imguiLabel("Save Scene to file");
    if (imguiButton(saveSceneName.c_str(), sceneLoaded)) {
        char *fileName = openSaveSceneFileDialog(lastLoadSceneFile.data());
        if (fileName) {
            setLastSaveFileName(fileName);
            std::string extension = saveSceneName.substr(saveSceneName.length() - 4, saveSceneName.length());
            std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
            std::string msg = "Saving Scene";
            if (extension == "JSON") {
                msg += ".Json";
            }
            msg += " file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            Logger::log(NK_INFO, "Done saving Scene.");
        }
    }
}
