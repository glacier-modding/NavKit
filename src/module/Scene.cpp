#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Scene.h"

#include <fstream>
#include <functional>

#include "../../include/NavKit/util/FileUtil.h"

Scene::Scene()
    : sceneLoaded(false)
      , sceneScroll(0)
      , loadSceneName("Load NavKit Scene")
      , saveSceneName("Save NavKit Scene")
{}

Scene::~Scene() {
}

char *Scene::openLoadSceneFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Nav.json files", "nav.json"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

char *Scene::openSaveSceneFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Nav.json files", "nav.json"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output");
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
    saveSceneName = saveSceneName.substr(saveSceneName.find_last_of("/\\") + 1);
}

void Scene::loadScene(const std::string &fileName, const std::function<void()> &callback,
                      const std::function<void()> &errorCallback) {
    sceneLoaded = false;
    Menu::updateMenuState();
    ZPathfinding::Alocs newAlocs;
    try {
        newAlocs = ZPathfinding::Alocs(fileName);
    } catch (const std::exception &e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    alocs = newAlocs.readAlocs();

    ZPathfinding::PfBoxes pfBoxes;
    try {
        pfBoxes = ZPathfinding::PfBoxes(fileName);
    } catch (const std::exception &e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    Navp &navp = Navp::getInstance();
    pfBoxes.readPathfindingBBoxes();
    if (includeBox.scale.x != -1) {
        // Swap Y and Z to go from Hitman's Z+ = Up coordinates to Recast's Y+ = Up coordinates
        // Negate Y position to go from Hitman's Z+ = North to Recast's Y- = North
        float pos[3] = {includeBox.pos.x, includeBox.pos.z, -includeBox.pos.y};
        float size[3] = {includeBox.scale.x, includeBox.scale.z, includeBox.scale.y};
        navp.setBBox(pos, size);
    }

    ZPathfinding::PfSeedPoints newPfSeedPoints;
    try {
        newPfSeedPoints = ZPathfinding::PfSeedPoints(fileName);
    } catch (const std::exception &e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    pfSeedPoints = newPfSeedPoints.readPfSeedPoints();
    callback();
}

void Scene::saveScene(char* fileName) {
    std::stringstream ss;

    ss << R"({"alocs":[)";
    const char* separator = "";
    for (const auto& aloc : alocs) {
        ss << separator;
        aloc.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"pfBoxes":[)";
    includeBox.writeJson(ss);
    for (const auto& pfBox : exclusionBoxes) {
        ss << ",";
        pfBox.writeJson(ss);
    }

    ss << R"(],"pfSeedPoints":[)";
    separator = "";
    for (auto& pfSeedPoint : pfSeedPoints) {
        ss << separator;
        pfSeedPoint.writeJson(ss);
        separator = ",";
    }
    ss << "]}";

    std::ofstream f(fileName);
    if (f.is_open()) {
        f << ss.rdbuf();
        f.close();
        Logger::log(NK_INFO, "Done saving Scene.");
    } else {
        Logger::log(NK_ERROR, "Failed to open file for saving scene: %s", fileName);
    }
}

void Scene::handleOpenScenePressed() {
    if (char *fileName = openLoadSceneFileDialog()) {
        setLastLoadFileName(fileName);
        std::string msg = "Loading nav.json file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        std::string fileNameToLoad = fileName;
        backgroundWorker.emplace(
                &Scene::loadScene,
                this,
                fileNameToLoad,
                [fileNameToLoad]() {
                    getInstance().sceneLoaded = true;
                    Menu::updateMenuState();
                    Logger::log(
                        NK_INFO,
                        ("Done loading nav.json file: '" + fileNameToLoad + "'.").
                        c_str());
                }, []() {
                    Logger::log(NK_ERROR, "Error loading scene file.");
                });
    }
}

void Scene::handleSaveScenePressed() {
    if (char *fileName = openSaveSceneFileDialog()) {
        setLastSaveFileName(fileName);
        std::string msg = "Saving NavKit Scene file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        backgroundWorker.emplace(&Scene::saveScene, this, fileName);
    }
}
