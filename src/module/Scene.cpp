#include "../../include/RecastDemo/imgui.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/module/Scene.h"

#include <fstream>
#include <functional>

#include "../../include/NavKit/util/FileUtil.h"

Scene::Scene() {
    sceneLoaded = false;
    sceneScroll = 0;
    loadSceneName = "Load NavKit Scene";
    saveSceneName = "Save NavKit Scene";
}

const int Scene::SCENE_MENU_HEIGHT = 125;

Scene::~Scene() {
}

char *Scene::openLoadSceneFileDialog(const char *lastSceneFolder) {
    nfdu8filteritem_t filters[2] = {{"Nav.json files", "nav.json"}};
    return FileUtil::openNfdLoadDialog(filters, 2, lastSceneFolder);
}

char *Scene::openSaveSceneFileDialog(char *lastSceneFolder) {
    nfdu8filteritem_t filters[2] = {{"Nav.json files", "nav.json"}};
    return FileUtil::openNfdSaveDialog(filters, 2, "output", lastSceneFolder);
}

void Scene::setLastLoadFileName(char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadSceneName = fileName;
        lastLoadSceneFile = loadSceneName.data();
        loadSceneName = loadSceneName.substr(loadSceneName.find_last_of("/\\") + 1);
    }
}

void Scene::setLastSaveFileName(char *fileName) {
    saveSceneName = fileName;
    lastSaveSceneFile = saveSceneName.data();
    saveSceneName = saveSceneName.substr(saveSceneName.find_last_of("/\\") + 1);
}

void Scene::loadScene(std::string fileName, const std::function<void()> &callback,
                      const std::function<void()> &errorCallback) {
    Scene &scene = getInstance();
    scene.sceneLoaded = false;
    ZPathfinding::Alocs newAlocs;
    try {
        newAlocs = ZPathfinding::Alocs(fileName);
    } catch (...) {
        errorCallback();
    }
    scene.alocs = newAlocs.readAlocs();

    ZPathfinding::PfBoxes pfBoxes;
    try {
        pfBoxes = ZPathfinding::PfBoxes(fileName);
    } catch (...) {
        errorCallback();
    }
    Navp &navp = Navp::getInstance();
    pfBoxes.readPathfindingBBoxes();
    if (scene.includeBox.scale.x != -1) {
        // Swap Y and Z to go from Hitman's Z+ = Up coordinates to Recast's Y+ = Up coordinates
        // Negate Y position to go from Hitman's Z+ = North to Recast's Y- = North
        float pos[3] = {scene.includeBox.pos.x, scene.includeBox.pos.z, -scene.includeBox.pos.y};
        float size[3] = {scene.includeBox.scale.x, scene.includeBox.scale.z, scene.includeBox.scale.y};
        navp.setBBox(pos, size);
    }

    ZPathfinding::PfSeedPoints newPfSeedPoints;
    try {
        newPfSeedPoints = ZPathfinding::PfSeedPoints(fileName);
    } catch (...) {
        errorCallback();
    }
    scene.pfSeedPoints = newPfSeedPoints.readPfSeedPoints();
    callback();
}

void Scene::saveScene(char *fileName) {
    Scene &scene = getInstance();
    std::ofstream f(fileName);
    f.clear();
    f << R"({"alocs":[)";
    bool first = true;
    for (auto aloc: scene.alocs) {
        if (!first) {
            f << ",";
        }
        first = false;
        aloc.writeJson(f);
    }
    f << R"(],"pfBoxes":[)";
    scene.includeBox.writeJson(f);
    for (auto pfBox: scene.exclusionBoxes) {
        f << ",";
        pfBox.writeJson(f);
    }
    f << R"(],"pfSeedPoints":[)";
    first = true;
    for (auto pfSeedPoint: scene.pfSeedPoints) {
        if (!first) {
            f << ",";
        }
        first = false;
        pfSeedPoint.writeJson(f);
    }
    f << "]}";
    f.close();
    Logger::log(NK_INFO, "Done saving Scene.");
}

void Scene::drawMenu() {
    Renderer &renderer = Renderer::getInstance();
    Gui &gui = Gui::getInstance();

    if (imguiBeginScrollArea("NavKit Scene menu", renderer.width - 250 - 10,
                             renderer.height - 10 - Settings::SETTINGS_MENU_HEIGHT - SCENE_MENU_HEIGHT - 5, 250,
                             SCENE_MENU_HEIGHT, &sceneScroll)) {
        gui.mouseOverMenu = true;
    }
    imguiLabel("Load NavKit Scene from file");
    if (imguiButton(loadSceneName.c_str())) {
        if (char *fileName = openLoadSceneFileDialog(lastLoadSceneFile.data())) {
            setLastLoadFileName(fileName);
            std::string msg = "Loading nav.json file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            std::string fileNameToLoad = fileName;
            auto loadSceneThread =
                    std::thread(
                        loadScene,
                        fileNameToLoad,
                        [fileNameToLoad]() {
                            getInstance().sceneLoaded = true;
                            Logger::log(
                                NK_INFO,
                                ("Done loading nav.json file: '" + fileNameToLoad + "'.").
                                c_str());
                        }, []() {
                            Logger::log(NK_ERROR, "Error loading scene file.");
                        });
            loadSceneThread.detach();
        }
    }
    imguiLabel("Save NavKit Scene to file");
    if (imguiButton(saveSceneName.c_str(), sceneLoaded)) {
        if (char *fileName = openSaveSceneFileDialog(lastLoadSceneFile.data())) {
            setLastSaveFileName(fileName);
            std::string msg = "Saving NavKit Scene file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            auto saveSceneThread = std::thread(saveScene, fileName);
            saveSceneThread.detach();
        }
    }
    imguiEndScrollArea();
}
