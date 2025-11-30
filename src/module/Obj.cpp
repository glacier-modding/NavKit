#include "../../include/NavKit/module/Obj.h"

#include <filesystem>
#include <fstream>
#include <thread>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavWeakness/NavPower.h"

Obj::Obj() {
    loadObjName = "Load Obj";
    saveObjName = "Save Obj";
    lastObjFileName = loadObjName;
    lastSaveObjFileName = saveObjName;
    objLoaded = false;
    showObj = true;
    objToLoad = "";
    loadObj = false;
    objScroll = 0;
    blenderPath = R"("C:\Program Files\Blender Foundation\Blender 3.4\blender.exe")";
    blenderSet = false;
    startedObjGeneration = false;
    glacier2ObjDebugLogsEnabled = false;
    blenderObjStarted = false;
    blenderObjGenerationDone = false;
    errorBuilding = false;
    doObjHitTest = false;
}

void Obj::setBlenderFile(const std::string& fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        blenderSet = true;
        blenderPath = fileName;
        Logger::log(NK_INFO, ("Setting Blender exe path to: " + blenderPath).c_str());
        Settings::setValue("Paths", "blender", fileName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find Blender exe path: " + blenderPath).c_str());
    }
}

void Obj::buildObjFromNavp(bool alsoLoadIntoUi) {
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    std::string fileName = sceneExtract.outputFolder + "\\outputNavp.obj";
    Gui &gui = Gui::getInstance();
    gui.showLog = true;

    Logger::log(NK_INFO, ("Building obj from loaded navp. Saving to: " + fileName).c_str());
    Navp &navp = Navp::getInstance();
    std::vector<Vec3> vertices;
    std::vector<std::vector<int> > faces;
    std::map<Vec3, int> pointsToIndexMap;
    int pointCount = 1;
    for (const auto &[m_area, m_edges]: navp.navMesh->m_areas) {
        std::vector<int> face;
        for (auto edge: m_edges) {
            auto point = edge->m_pos;
            if (!pointsToIndexMap.contains(point)) {
                vertices.push_back(point);
                pointsToIndexMap[point] = pointCount++;
            }
            face.push_back(pointsToIndexMap[point]);
        }
        faces.push_back(face);
    }
    std::ofstream f(fileName);
    f.clear();
    for (auto v: vertices) {
        f << "v " << v.X << " " << v.Z << " " << -v.Y << "\n";
    }
    for (auto face: faces) {
        f << "f ";
        for (auto vi: face) {
            f << vi;
            if (vi != face.back()) {
                f << " ";
            }
        }
        f << "\n";
    }
    f.close();
    Logger::log(NK_INFO, "Done Building obj from loaded navp.");
    if (alsoLoadIntoUi) {
        generatedObjName = "outputNavp.obj";
        objLoaded = false;
        blenderObjGenerationDone = true;
        loadObj = true;
        Menu::updateMenuState();
    }
}

void Obj::buildObj(const char *blenderPath, const char *sceneFilePath, const char *outputFolder) {
    objLoaded = false;
    Menu::updateMenuState();
    startedObjGeneration = true;
    Logger::log(NK_INFO, "Generating obj from nav.json file.");
    std::string command = "\"";
    command += blenderPath;
    command += "\" -b --factory-startup -P glacier2obj.py -- \""; //--debug-all
    command += sceneFilePath;
    command += "\" \"";
    command += outputFolder;
    command += "\\output.obj\"";
    if (glacier2ObjDebugLogsEnabled) {
        command += " True";
    }
    blenderObjStarted = true;
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    generatedObjName = "output.obj";

    backgroundWorker.emplace(
        &CommandRunner::runCommand, CommandRunner::getInstance(), command, "Glacier2ObjBlender.log", [this] {
            Logger::log(NK_INFO, "Finished generating obj from nav.json file.");
            objLoaded = false;
            blenderObjGenerationDone = true;
            Menu::updateMenuState();
        }, [this] {
            objLoaded = false;
            errorBuilding = true;
            Menu::updateMenuState();
        });
}

char *Obj::openSetBlenderFileDialog(const char *lastBlenderFile) {
    nfdu8filteritem_t filters[1] = {{"Exe files", "exe"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

void Obj::finalizeObjBuild() {
    if (blenderObjGenerationDone) {
        Obj &obj = getInstance();
        startedObjGeneration = false;
        const SceneExtract &sceneExtract = SceneExtract::getInstance();
        obj.objToLoad = sceneExtract.outputFolder;
        obj.objToLoad += "\\" + generatedObjName;
        obj.loadObj = true;
        obj.lastObjFileName = sceneExtract.outputFolder;
        obj.lastObjFileName += generatedObjName;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
    }
    if (errorBuilding) {
        errorBuilding = false;
        startedObjGeneration = false;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
    }
}

void Obj::copyObjFile(const std::string &from, const std::string &to) {
    if (from == to) {
        Logger::log(NK_ERROR, "Cannot overwrite current obj file: %s", from.c_str());
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();
    Logger::log(NK_INFO, "Copying OBJ from '%s' to '%s'...", from.c_str(), to.c_str());

    try {
        std::filesystem::copy(from, to, std::filesystem::copy_options::overwrite_existing);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        Logger::log(NK_INFO, "Finished saving Obj in %lld ms.", duration.count());
    } catch (const std::filesystem::filesystem_error &e) {
        Logger::log(NK_ERROR, "Error copying file: %s", e.what());
    }
}

void Obj::saveObjMesh(char *objToCopy, char *newFileName) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Obj to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    backgroundWorker.emplace(&Obj::copyObjFile, objToCopy, newFileName);
}

void Obj::loadObjMesh() {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Obj from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();
    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    if (recastAdapter.loadInputGeom(objToLoad)) {
        if (objLoadDone.empty()) {
            objLoadDone.push_back(true);
            Scene &scene = Scene::getInstance();
            float pos[3] = {
                scene.bBoxPos[0],
                scene.bBoxPos[1],
                scene.bBoxPos[2]
            };
            float size[3] = {
                scene.bBoxScale[0],
                scene.bBoxScale[1],
                scene.bBoxScale[2]
            };
            scene.setBBox(pos, size);
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
            msg = "Finished loading Obj in ";
            msg += std::to_string(duration.count());
            msg += " seconds";
            Logger::log(NK_INFO, msg.data());
        }
    } else {
        Logger::log(NK_ERROR, "Error loading obj.");
    }
    objToLoad.clear();
}

void Obj::renderObj() {
    RecastAdapter::getInstance().drawInputGeom();
}

char *Obj::openLoadObjFileDialog(const char *lastObjFolder) {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}


char *Obj::openSaveObjFileDialog(char *lastObjFolder) {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output");
}

void Obj::setLastLoadFileName(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadObjName = fileName;
        lastObjFileName = loadObjName;
        loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
    }
}

void Obj::setLastSaveFileName(const char *fileName) {
    lastSaveObjFileName = fileName;
    loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
}

void Obj::handleOpenObjClicked() {
    char *fileName = openLoadObjFileDialog(loadObjName.data());
    if (fileName) {
        setLastLoadFileName(fileName);
        objLoaded = false;
        objToLoad = fileName;
        loadObj = true;
        Menu::updateMenuState();
    }
}

void Obj::handleSaveObjClicked() {
    char *fileName = openSaveObjFileDialog(loadObjName.data());
    if (fileName) {
        loadObjName = fileName;
        setLastSaveFileName(fileName);
        saveObjMesh(lastObjFileName.data(), lastSaveObjFileName.data());
        saveObjName = loadObjName;
    }
}

bool Obj::canLoad() const {
    return objToLoad.empty();
}

bool Obj::canBuildObjFromNavp() const {
    const SceneExtract &sceneExtract = SceneExtract::getInstance();
    const Navp &navp = Navp::getInstance();
    return sceneExtract.outputSet && blenderSet && navp.navpLoaded;
}

bool Obj::canBuildObjFromScene() const {
    const SceneExtract &sceneExtract = SceneExtract::getInstance();
    const Scene &scene = Scene::getInstance();
    return sceneExtract.outputSet && blenderSet && scene.sceneLoaded && !
           sceneExtract.extractingAlocs && !blenderObjStarted && !
           blenderObjGenerationDone;
}

void Obj::handleBuildObjFromSceneClicked() {
    const SceneExtract &sceneExtract = SceneExtract::getInstance();
    const Scene &scene = Scene::getInstance();
    return buildObj(blenderPath.data(), scene.lastLoadSceneFile.data(), sceneExtract.outputFolder.data());
}

void Obj::handleBuildObjFromNavpClicked() {
    return buildObjFromNavp(true);
}

void Obj::finalizeLoad() {
    if (loadObj) {
        lastObjFileName = objToLoad;
        objectTriangleRanges.clear();
        RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        recastAdapter.selectedObject = "";
        recastAdapter.markerPositionSet = false;
        std::string msg = "Loading Obj file: '";
        msg += objToLoad;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        backgroundWorker.emplace(&Obj::loadObjMesh, this);
        loadObj = false;
    }

    if (!objLoadDone.empty()) {
        objLoaded = true;
        RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        if (recastAdapter.inputGeom) {
            recastAdapter.handleMeshChanged();
            Navp::getInstance().updateExclusionBoxConvexVolumes();
        }
        objLoadDone.clear();
        Menu::updateMenuState();
    }
}
