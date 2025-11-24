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

#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"

#include "../../include/RecastDemo/imgui.h"

SceneExtract::SceneExtract() {
    hitmanFolderName = "Choose Hitman folder";
    lastHitmanFolder = hitmanFolderName;
    hitmanSet = false;
    outputFolderName = "Choose Output folder";
    lastOutputFolder = outputFolderName;
    outputSet = false;
    extractScroll = 0;
    errorExtracting = false;
    doneExtractingAlocs = false;
    extractingAlocs = false;
    doneExtractingFromGame = false;
    extractingFromGame = false;
    alsoBuildObj = false;
}

const int SceneExtract::SCENE_EXTRACT_MENU_HEIGHT = 83;

SceneExtract::~SceneExtract() = default;

void SceneExtract::setHitmanFolder(const char *folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        hitmanSet = true;
        lastHitmanFolder = folderName;
        hitmanFolderName = folderName;
        hitmanFolderName = hitmanFolderName.substr(hitmanFolderName.find_last_of("/\\") + 1);
        Logger::log(NK_INFO, ("Setting Hitman folder to: " + lastHitmanFolder).c_str());
        Settings::setValue("Paths", "hitman", folderName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find Hitman folder: " + lastHitmanFolder).c_str());
    }
}

void SceneExtract::setOutputFolder(const char *folderName) {
    if (std::filesystem::exists(folderName) && std::filesystem::is_directory(folderName)) {
        outputSet = true;
        lastOutputFolder = folderName;
        outputFolderName = folderName;
        outputFolderName = outputFolderName.substr(outputFolderName.find_last_of("/\\") + 1);
        Logger::log(NK_INFO, ("Setting output folder to: " + lastOutputFolder).c_str());
        Settings::setValue("Paths", "output", folderName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find output folder: " + lastOutputFolder).c_str());
    }
}

void SceneExtract::handleExtractFromGameClicked() {
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    alsoBuildObj = false;
    extractScene();
}

void SceneExtract::drawMenu() {
    if (const Renderer &renderer = Renderer::getInstance();
        imguiBeginScrollArea("Extract menu", renderer.width - 250 - 10,
                             renderer.height - 10 -
                             Settings::SETTINGS_MENU_HEIGHT -
                             Scene::SCENE_MENU_HEIGHT -
                             SCENE_EXTRACT_MENU_HEIGHT - 10, 250,
                             SCENE_EXTRACT_MENU_HEIGHT, &extractScroll)) {
        Gui::getInstance().mouseOverMenu = true;
    }
    if (imguiButton("Extract from game", hitmanSet && outputSet && !extractingFromGame && !extractingAlocs)) {
        handleExtractFromGameClicked();
    }
    if (const Obj &obj = Obj::getInstance();
        imguiButton("Extract from game and build obj",
                    hitmanSet && obj.blenderSet && outputSet && !extractingFromGame
                    && !extractingAlocs && !obj.blenderObjStarted && !obj.blenderObjGenerationDone)) {
        Gui &gui = Gui::getInstance();
        gui.showLog = true;
        alsoBuildObj = true;
        Obj::getInstance().objLoaded = false;
        extractScene();
    }
    imguiEndScrollArea();
}

void SceneExtract::extractFromGame(const std::function<void()> &callback, const std::function<void()> &errorCallback) {
    GameConnection &gameConnection = GameConnection::getInstance();
    std::ofstream f(getInstance().lastOutputFolder + "\\output.nav.json");
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
        extractAlocs();
    }, [this, &gameConnection] {
        errorExtracting = true;
        if (gameConnection.closeConnection()) {
            Logger::log(NK_ERROR, "Error closing connection to game.");
        }
    });
}

void SceneExtract::extractAlocs() {
    std::string retailFolder = "\"";
    retailFolder += lastHitmanFolder;
    retailFolder += "\\Retail\"";
    std::string gameVersion = "HM3";
    std::string navJsonFilePath = "\"";
    navJsonFilePath += lastOutputFolder;
    navJsonFilePath += "\\output.nav.json\"";
    std::string runtimeFolder = "\"";
    runtimeFolder += lastHitmanFolder;
    runtimeFolder += "\\Runtime\"";
    std::string alocFolder;
    alocFolder += lastOutputFolder;
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
    std::jthread commandThread(
        &CommandRunner::runCommand, CommandRunner::getInstance(), command,
        "Glacier2ObjExtract.log", [this] {
            Logger::log(NK_INFO, "Finished extracting scene from game to nav.json file.");
            extractingAlocs = false;
            doneExtractingAlocs = true;
        }, [this] {
            errorExtracting = true;
        });
}

void SceneExtract::finalizeExtract() {
    Obj &obj = Obj::getInstance();
    if (doneExtractingAlocs) {
        doneExtractingAlocs = false;
        std::string sceneFile = lastOutputFolder;
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
                    objScoped.buildObj(objScoped.lastBlenderFile.data(), fileNameString.data(),
                                       sceneExtract.lastOutputFolder.data());
                }
                Logger::log(NK_INFO, ("Done loading nav.json file: '" + fileNameString + "'.").c_str());
            }, []() {
                SceneExtract &sceneExtract = getInstance();
                Obj &objScoped = Obj::getInstance();
                Logger::log(NK_ERROR, "Error loading scene file.");
                sceneExtract.errorExtracting = false;
                objScoped.startedObjGeneration = false;
                sceneExtract.extractingFromGame = false;
                sceneExtract.extractingAlocs = false;
            });
    }
    if (errorExtracting) {
        errorExtracting = false;
        obj.startedObjGeneration = false;
        extractingFromGame = false;
        extractingAlocs = false;
    }
}

char *SceneExtract::openHitmanFolderDialog(char *lastHitmanFolder) {
    return FileUtil::openNfdFolderDialog(lastHitmanFolder);
}

char *SceneExtract::openOutputFolderDialog(char *lastOutputFolder) {
    return FileUtil::openNfdFolderDialog(lastOutputFolder);
}
