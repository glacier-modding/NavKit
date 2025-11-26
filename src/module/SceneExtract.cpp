#include <direct.h>
#include <string>
#include <vector>

#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/GameConnection.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"

#include <fstream>

#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"

SceneExtract::SceneExtract() {
    hitmanFolder = "";
    hitmanSet = false;
    outputFolder = "";
    outputSet = false;
    extractScroll = 0;
    errorExtracting = false;
    doneExtractingAlocs = false;
    extractingAlocs = false;
    doneExtractingFromGame = false;
    extractingFromGame = false;
    alsoBuildObj = false;
}

SceneExtract::~SceneExtract() = default;

void SceneExtract::setHitmanFolder(const std::string &folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        hitmanSet = true;
        hitmanFolder = folderName;
        Logger::log(NK_INFO, ("Setting Hitman folder to: " + hitmanFolder).c_str());
        Settings::setValue("Paths", "hitman", folderName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find Hitman folder: " + hitmanFolder).c_str());
    }
}

void SceneExtract::setOutputFolder(const std::string &folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        outputSet = true;
        outputFolder = folderName;
        Logger::log(NK_INFO, ("Setting output folder to: " + outputFolder).c_str());
        Settings::setValue("Paths", "output", folderName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find output folder: " + outputFolder).c_str());
    }
}

void SceneExtract::handleExtractFromGameClicked() {
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    alsoBuildObj = false;
    extractScene();
}

bool SceneExtract::canExtractFromGame() const {
    return hitmanSet && outputSet && !extractingFromGame && !extractingAlocs;
}

bool SceneExtract::canExtractFromGameAndBuildObj() const {
    const Obj &obj = Obj::getInstance();
    return canExtractFromGame()
           && obj.blenderSet && !obj.blenderObjStarted && !obj.blenderObjGenerationDone;
}

void SceneExtract::handleExtractFromGameAndBuildObjClicked() {
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    alsoBuildObj = true;
    Obj::getInstance().objLoaded = false;
    extractScene();
}

void SceneExtract::extractFromGame(const std::function<void()> &callback, const std::function<void()> &errorCallback) {
    GameConnection &gameConnection = GameConnection::getInstance();
    std::ofstream f(getInstance().outputFolder + "\\output.nav.json");
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
        extractAlocs();
    }, [this, &gameConnection] {
        errorExtracting = true;
        Menu::updateMenuState();
        if (gameConnection.closeConnection()) {
            Logger::log(NK_ERROR, "Error closing connection to game.");
        }
    });
}

void SceneExtract::extractAlocs() {
    std::string retailFolder = "\"";
    retailFolder += hitmanFolder;
    retailFolder += "\\Retail\"";
    std::string gameVersion = "HM3";
    std::string navJsonFilePath = "\"";
    navJsonFilePath += outputFolder;
    navJsonFilePath += "\\output.nav.json\"";
    std::string runtimeFolder = "\"";
    runtimeFolder += hitmanFolder;
    runtimeFolder += "\\Runtime\"";
    std::string alocFolder;
    alocFolder += outputFolder;
    alocFolder += "\\aloc";

    struct stat folderExists{};
    if (int statRC = stat(alocFolder.data(), &folderExists); statRC != 0) {
        if (errno == ENOENT) {
            if (int status = _mkdir(alocFolder.c_str()); status != 0) {
                Logger::log(NK_ERROR, "Error creating prim folder");
            }
        }
    }

    std::string command = "Glacier2Obj.exe ";
    command += retailFolder;
    command += " ";
    command += gameVersion;
    command += " ";
    command += navJsonFilePath;
    command += " ";
    command += runtimeFolder;
    command += " \"";
    command += alocFolder;
    command += "\"";
    extractingAlocs = true;
    Menu::updateMenuState();
    std::jthread commandThread(
        &CommandRunner::runCommand, CommandRunner::getInstance(), command,
        "Glacier2ObjExtract.log", [this] {
            Logger::log(NK_INFO, "Finished extracting scene from game to nav.json file.");
            extractingAlocs = false;
            doneExtractingAlocs = true;
            Menu::updateMenuState();
        }, [this] {
            errorExtracting = true;
            Menu::updateMenuState();
        });
}

void SceneExtract::finalizeExtract() {
    Obj &obj = Obj::getInstance();
    if (doneExtractingAlocs) {
        doneExtractingAlocs = false;
        std::string sceneFile = outputFolder;
        sceneFile += "\\output.nav.json";
        backgroundWorker.emplace(
            &Scene::loadScene,
            &Scene::getInstance(),
            sceneFile,
            [sceneFile]() {
                Scene &sceneScoped = Scene::getInstance();
                SceneExtract &sceneExtract = getInstance();
                Obj &objScoped = Obj::getInstance();
                sceneScoped.sceneLoaded = true;
                std::string fileNameString = sceneFile;
                sceneExtract.extractingAlocs = false;
                sceneScoped.lastLoadSceneFile = sceneFile;
                if (sceneExtract.alsoBuildObj && !objScoped.startedObjGeneration) {
                    objScoped.buildObj(objScoped.blenderPath.data(), fileNameString.data(),
                                       sceneExtract.outputFolder.data());
                }
                Logger::log(NK_INFO, ("Done loading nav.json file: '" + fileNameString + "'.").c_str());
                Menu::updateMenuState();
            }, []() {
                SceneExtract &sceneExtract = getInstance();
                Obj &objScoped = Obj::getInstance();
                Logger::log(NK_ERROR, "Error loading scene file.");
                sceneExtract.errorExtracting = false;
                objScoped.startedObjGeneration = false;
                sceneExtract.extractingFromGame = false;
                sceneExtract.extractingAlocs = false;
                Menu::updateMenuState();
            });
    }
    if (errorExtracting) {
        errorExtracting = false;
        obj.startedObjGeneration = false;
        extractingFromGame = false;
        extractingAlocs = false;
        Menu::updateMenuState();
    }
}

char *SceneExtract::openHitmanFolderDialog(char *lastHitmanFolder) {
    return FileUtil::openNfdFolderDialog(lastHitmanFolder);
}

char *SceneExtract::openOutputFolderDialog(char *lastOutputFolder) {
    return FileUtil::openNfdFolderDialog(lastOutputFolder);
}
