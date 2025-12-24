#pragma once
#include <fstream>

#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/module/GameConnection.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/util/FileUtil.h"

SceneExtract::SceneExtract() {
    doneExtractingFromGame = false;
    extractingFromGame = false;
    alsoBuildObj = false;
    alsoBuildAll = false;
}

SceneExtract::~SceneExtract() = default;

void SceneExtract::handleExtractFromGameClicked() {
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    alsoBuildObj = false;
    alsoBuildAll = false;
    extractScene();
}

bool SceneExtract::canExtractFromGame() const {
    return !extractingFromGame;
}

bool SceneExtract::canExtractFromGameAndBuildObj() const {
    const Obj &obj = Obj::getInstance();
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    return canExtractFromGame()
           && navKitSettings.blenderSet && !obj.blenderObjStarted && !obj.blenderObjGenerationDone;
}

bool SceneExtract::canExtractFromGameAndBuildAll() const {
    const Obj &obj = Obj::getInstance();
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    return canExtractFromGame()
           && navKitSettings.blenderSet && !obj.blenderObjStarted && !obj.blenderObjGenerationDone;
}

void SceneExtract::handleExtractFromGameAndBuildObjClicked() {
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    alsoBuildObj = true;
    Obj::getInstance().objLoaded = false;
    extractScene();
}

void SceneExtract::handleExtractFromGameAndBuildAllClicked() {
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    alsoBuildAll = true;
    Obj::getInstance().objLoaded = false;
    extractScene();
}

void SceneExtract::extractFromGame(const std::function<void()> &callback, const std::function<void()> &errorCallback) {
    GameConnection &gameConnection = GameConnection::getInstance();
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    std::ofstream f(navKitSettings.outputFolder + "\\output.nav.json");
    f.clear();
    f.close();

    if (gameConnection.connectToGame()) {
        errorCallback();
        return;
    }
    if (gameConnection.listAlocPfBoxAndSeedPointEntities()) {
        errorCallback();
        return;
    }
    if (gameConnection.closeConnection()) {
        errorCallback();
        return;
    }
    callback();
}

void SceneExtract::extractScene() {
    Logger::log(NK_INFO, "Extracting scene from game...");
    GameConnection &gameConnection = GameConnection::getInstance();
    extractingFromGame = true;

    backgroundWorker.emplace(extractFromGame, [this] {
        Logger::log(NK_INFO, "Finished extracting scene from game to nav.json file.");
        extractingFromGame = false;
        doneExtractingFromGame = true;
        Menu::updateMenuState();
    }, [this, &gameConnection] {
        extractingFromGame = false;
        doneExtractingFromGame = false;
        Menu::updateMenuState();
        if (gameConnection.closeConnection()) {
            Logger::log(NK_ERROR, "Error closing connection to game.");
        }
    });
}

void SceneExtract::finalizeExtractScene() {
    if (doneExtractingFromGame) {
        doneExtractingFromGame = false;
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
        Scene &scene = Scene::getInstance();
        std::string sceneFile = navKitSettings.outputFolder;
        sceneFile += "\\output.nav.json";
        Logger::log(NK_INFO, "Loading nav.json file: '%s'.", sceneFile.c_str());

        backgroundWorker.emplace(
            &Scene::loadScene,
            &scene,
            sceneFile,
            [sceneFile, this] {
                Scene &sceneScoped = Scene::getInstance();
                Obj &obj = Obj::getInstance();
                sceneScoped.sceneLoaded = true;
                const std::string &fileNameString = sceneFile;
                sceneScoped.lastLoadSceneFile = sceneFile;
                if ((alsoBuildObj || alsoBuildAll) && !obj.startedObjGeneration) {
                    obj.extractAlocsOrPrimsAndStartObjBuild();
                }
                Logger::log(NK_INFO, ("Done loading nav.json file: '" + fileNameString + "'.").c_str());
                Menu::updateMenuState();
            }, [this] {
                Obj &objScoped = Obj::getInstance();
                Logger::log(NK_ERROR, "Error loading scene file.");
                objScoped.startedObjGeneration = false;
                extractingFromGame = false;
                alsoBuildAll = false;
                alsoBuildObj = false;
                Menu::updateMenuState();
            });
    }
}

char *SceneExtract::openHitmanFolderDialog(char *lastHitmanFolder) {
    return FileUtil::openNfdFolderDialog(lastHitmanFolder);
}

char *SceneExtract::openOutputFolderDialog(char *lastOutputFolder) {
    return FileUtil::openNfdFolderDialog(lastOutputFolder);
}