#include "../../include/NavKit/module/Obj.h"

#include <filesystem>
#include <fstream>
#include <thread>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavWeakness/NavPower.h"
#include "../../include/RecastDemo/imgui.h"

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
    lastBlenderFile = R"("C:\Program Files\Blender Foundation\Blender 3.4\blender.exe")";
    blenderName = "Choose Blender app";
    blenderSet = false;
    startedObjGeneration = false;
    glacier2ObjDebugLogsEnabled = false;
    blenderObjStarted = false;
    blenderObjGenerationDone = false;
    errorBuilding = false;
    doObjHitTest = false;
}

const int Obj::OBJ_MENU_HEIGHT = 174;

void Obj::setBlenderFile(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        blenderSet = true;
        lastBlenderFile = fileName;
        blenderName = fileName;
        blenderName = blenderName.substr(blenderName.find_last_of("/\\") + 1);
        Logger::log(NK_INFO, ("Setting Blender exe to: " + lastBlenderFile).c_str());
        Settings::setValue("Paths", "blender", fileName);
        Settings::save();
    } else {
        Logger::log(NK_WARN, ("Could not find Blender exe: " + lastBlenderFile).c_str());
    }
}

void Obj::buildObjFromNavp(bool alsoLoadIntoUi) {
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    std::string fileName = sceneExtract.lastOutputFolder + "\\outputNavp.obj";
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
        Obj &obj = getInstance();
        obj.generatedObjName = "outputNavp.obj";
        obj.objLoaded = false;
        obj.blenderObjGenerationDone = true;
        obj.loadObj = true;
    }
}

void Obj::buildObj(char *blenderPath, char *sceneFilePath, char *outputFolder) {
    objLoaded = false;
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

    std::thread commandThread(CommandRunner::runCommand, command, "Glacier2ObjBlender.log", [this] {
        Logger::log(NK_INFO, "Finished generating obj from nav.json file.");
        objLoaded = false;
        blenderObjGenerationDone = true;
    }, [this] {
        objLoaded = false;
        errorBuilding = true;
    });
    commandThread.detach();
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
        obj.objToLoad = sceneExtract.lastOutputFolder;
        obj.objToLoad += "\\" + generatedObjName;
        obj.loadObj = true;
        obj.lastObjFileName = sceneExtract.lastOutputFolder;
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

    } catch (const std::filesystem::filesystem_error& e) {
        Logger::log(NK_ERROR, "Error copying file: %s", e.what());
    }
}

void Obj::saveObjMesh(char *objToCopy, char *newFileName) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Obj to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    std::thread saveObjThread(&Obj::copyObjFile, objToCopy, newFileName);
    saveObjThread.detach();
}

void Obj::loadObjMesh(Obj *obj) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Obj from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();

    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    if (recastAdapter.loadInputGeom(obj->objToLoad)) {
        if (obj->objLoadDone.empty()) {
            obj->objLoadDone.push_back(true);
            Navp &navp = Navp::getInstance();
            float pos[3] = {
                navp.bBoxPos[0],
                navp.bBoxPos[1],
                navp.bBoxPos[2]
            };
            float size[3] = {
                navp.bBoxScale[0],
                navp.bBoxScale[1],
                navp.bBoxScale[2]
            };
            navp.setBBox(pos, size);
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
    obj->objToLoad.clear();
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

void Obj::handleOpenObjPressed() {
    char *fileName = openLoadObjFileDialog(loadObjName.data());
    if (fileName) {
        setLastLoadFileName(fileName);
        objLoaded = false;
        objToLoad = fileName;
        loadObj = true;
    }
}

void Obj::handleSaveObjPressed() {
    char *fileName = openSaveObjFileDialog(loadObjName.data());
    if (fileName) {
        loadObjName = fileName;
        setLastSaveFileName(fileName);
        saveObjMesh(lastObjFileName.data(), lastSaveObjFileName.data());
        saveObjName = loadObjName;
    }
}

void Obj::drawMenu() {
    Gui &gui = Gui::getInstance();
    Renderer &renderer = Renderer::getInstance();
    if (imguiBeginScrollArea("Obj menu", renderer.width - 250 - 10,
                             renderer.height - 10 - Settings::SETTINGS_MENU_HEIGHT - Scene::SCENE_MENU_HEIGHT -
                             SceneExtract::SCENE_EXTRACT_MENU_HEIGHT - OBJ_MENU_HEIGHT - 15, 250, OBJ_MENU_HEIGHT,
                             &objScroll)) {
        gui.mouseOverMenu = true;
    }

    imguiLabel("Load Obj file");
    if (imguiButton(loadObjName.c_str(), objToLoad.empty())) {
        handleOpenObjPressed();
    }
    imguiLabel("Save Obj file");
    if (imguiButton(saveObjName.c_str(), objLoaded)) {
        handleSaveObjPressed();
    }
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    if (Scene &scene = Scene::getInstance(); imguiButton("Build obj from NavKit Scene",
                                                         sceneExtract.outputSet && blenderSet && scene.sceneLoaded && !
                                                         sceneExtract.extractingAlocs && !blenderObjStarted && !
                                                         blenderObjGenerationDone)) {
        buildObj(lastBlenderFile.data(), scene.lastLoadSceneFile.data(), sceneExtract.lastOutputFolder.data());
    }
    Navp &navp = Navp::getInstance();
    if (imguiButton("Build obj from Navp",
                    sceneExtract.outputSet && blenderSet && navp.navpLoaded)) {
        buildObjFromNavp(true);
    }
    imguiEndScrollArea();
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
        std::thread loadObjThread(&Obj::loadObjMesh, this);
        loadObjThread.detach();
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
    }
}
