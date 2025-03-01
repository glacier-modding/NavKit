#include "../../include/NavKit/module/Obj.h"

#include <filesystem>
#include <fstream>
#include <thread>

#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/util/FileUtil.h"
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
    bBoxPos[0] = 0;
    bBoxPos[1] = 0;
    bBoxPos[2] = 0;
    bBoxSize[0] = 600;
    bBoxSize[1] = 600;
    bBoxSize[2] = 600;
    Logger::log(NK_INFO,
                ("Setting bbox to (" + std::to_string(bBoxPos[0]) + ", " + std::to_string(bBoxPos[1]) + ", " +
                 std::to_string(bBoxPos[2]) + ") (" + std::to_string(bBoxSize[0]) + ", " + std::to_string(bBoxSize[1]) +
                 ", " + std::to_string(bBoxSize[2]) + ")").c_str());
}

void Obj::copyObjFile(const std::string &from, const std::string &to) {
    if (from == to) {
        Logger::log(NK_ERROR, ("Cannot overwrite current obj file: " + from).c_str());
        return;
    }
    auto start = std::chrono::high_resolution_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    Obj &obj = getInstance();
    std::string navKitObjProperties = "";
    navKitObjProperties += "# NavKit OBJ Properties:\n";
    navKitObjProperties += "#![BBox]\n";
    navKitObjProperties += "#!x = " + std::to_string(obj.bBoxPos[0]) + "\n";
    navKitObjProperties += "#!y = " + std::to_string(obj.bBoxPos[1]) + "\n";
    navKitObjProperties += "#!z = " + std::to_string(obj.bBoxPos[2]) + "\n";
    navKitObjProperties += "#!sx = " + std::to_string(obj.bBoxSize[0]) + "\n";
    navKitObjProperties += "#!sy = " + std::to_string(obj.bBoxSize[1]) + "\n";
    navKitObjProperties += "#!sz = " + std::to_string(obj.bBoxSize[2]) + "\n";
    std::ifstream inputFile(from);
    if (!inputFile.is_open()) {
        Logger::log(NK_ERROR, ("Error opening obj file for reading: " + from).c_str());
        return;
    }
    std::ofstream outputFile(to);
    if (!outputFile.is_open()) {
        Logger::log(NK_ERROR, ("Error opening file for writing: " + to).c_str());
        return;
    }
    outputFile << navKitObjProperties << std::endl;

    std::vector<std::string> lines;
    std::string line;
    if (std::getline(inputFile, line)) {
        if (line.find("NavKit") != std::string::npos) {
            // Input obj already has NavKit OBJ Properties. Override them with the latest from NavKit
            for (int i = 0; i < 8; i++) {
                if (!std::getline(inputFile, line)) {
                    break;
                }
            }
        } else {
            outputFile << line << std::endl;
        }
    }

    while (std::getline(inputFile, line)) {
        outputFile << line << std::endl;
    }
    inputFile.close();
    outputFile.close();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    std::string msg = "Finished saving Obj in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.data());
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

    std::ifstream inputFile(obj->objToLoad);
    if (!inputFile.is_open()) {
        Logger::log(NK_ERROR, ("Error opening obj file for reading: " + obj->objToLoad).c_str());
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    if (std::getline(inputFile, line)) {
        if (line.find("NavKit") != std::string::npos) {
            std::getline(inputFile, line); // Get the BBox line
            float floatValues[6]{0, 0, 0, 100, 100, 100};
            bool error = false;
            for (int i = 0; i < 6; i++) {
                std::getline(inputFile, line);
                std::stringstream ss(line);
                std::string propertyName;
                if (getline(ss, propertyName, '=')) {
                    std::string value;
                    if (getline(ss, value)) {
                        try {
                            floatValues[i] = stof(value);
                        } catch (const std::invalid_argument &e) {
                            Logger::log(NK_ERROR,
                                        ("Error converting value to float for property " + propertyName + ": " + e.
                                         what()).c_str());
                            error = true;
                            break;
                        }
                        catch (const std::out_of_range &e) {
                            Logger::log(NK_ERROR,
                                        ("Value out of range for float for property " + propertyName + ": " + e.
                                         what()).c_str());
                            error = true;
                            break;
                        }
                    }
                }
            }
            if (!error) {
                obj->bBoxPos[0] = floatValues[0];
                obj->bBoxPos[1] = floatValues[1];
                obj->bBoxPos[2] = floatValues[2];
                obj->bBoxSize[0] = floatValues[3];
                obj->bBoxSize[1] = floatValues[4];
                obj->bBoxSize[2] = floatValues[5];
            }
        }
    }
    inputFile.close();

    if (RecastAdapter::getInstance().loadInputGeom(obj->objToLoad)) {
        if (obj->objLoadDone.empty()) {
            float pos[3] = {
                obj->bBoxPos[0],
                obj->bBoxPos[1],
                obj->bBoxPos[2]
            };
            float size[3] = {
                obj->bBoxSize[0],
                obj->bBoxSize[1],
                obj->bBoxSize[2]
            };
            obj->setBBox(pos, size);
            obj->objLoadDone.push_back(true);
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

void Obj::setBBox(float *pos, float *size) {
    Navp &navp = Navp::getInstance();
    bBoxPos[0] = pos[0];
    navp.bBoxPosX = pos[0];
    bBoxPos[1] = pos[1];
    navp.bBoxPosY = pos[1];
    bBoxPos[2] = pos[2];
    navp.bBoxPosZ = pos[2];
    bBoxSize[0] = size[0];
    navp.bBoxSizeX = size[0];
    bBoxSize[1] = size[1];
    navp.bBoxSizeY = size[1];
    bBoxSize[2] = size[2];
    navp.bBoxSizeZ = size[2];
    const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    const float bBoxMin[3] = {
        bBoxPos[0] - bBoxSize[0] / 2,
        bBoxPos[1] - bBoxSize[1] / 2,
        bBoxPos[2] - bBoxSize[2] / 2
    };
    const float bBoxMax[3] = {
        bBoxPos[0] + bBoxSize[0] / 2,
        bBoxPos[1] + bBoxSize[1] / 2,
        bBoxPos[2] + bBoxSize[2] / 2
    };
    recastAdapter.setMeshBBox(bBoxMin, bBoxMax);
    Logger::log(NK_INFO,
                ("Setting bbox to (" + std::to_string(pos[0]) + ", " + std::to_string(pos[1]) + ", " +
                 std::to_string(pos[2]) + ") (" + std::to_string(size[0]) + ", " + std::to_string(size[1]) + ", " +
                 std::to_string(size[2]) + ")").c_str());
    Settings::setValue("BBox", "x", std::to_string(pos[0]));
    Settings::setValue("BBox", "y", std::to_string(pos[1]));
    Settings::setValue("BBox", "z", std::to_string(pos[2]));
    Settings::setValue("BBox", "sx", std::to_string(size[0]));
    Settings::setValue("BBox", "sy", std::to_string(size[1]));
    Settings::setValue("BBox", "sz", std::to_string(size[2]));
    Settings::save();
}

void Obj::renderObj() {
    RecastAdapter::getInstance().drawInputGeom();
}

char *Obj::openLoadObjFileDialog(char *lastObjFolder) {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
    return FileUtil::openNfdLoadDialog(filters, 1, lastObjFolder);
}


char *Obj::openSaveObjFileDialog(char *lastObjFolder) {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output", lastObjFolder);
}

void Obj::setLastLoadFileName(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadObjName = fileName;
        lastObjFileName = loadObjName;
        loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
        Settings::setValue("Paths", "loadobj", fileName);
        Settings::save();
    }
}

void Obj::setLastSaveFileName(const char *fileName) {
    lastSaveObjFileName = fileName;
    loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
    Settings::setValue("Paths", "saveobj", fileName);
    Settings::save();
}

void Obj::drawMenu() {
    Gui &gui = Gui::getInstance();
    Renderer &renderer = Renderer::getInstance();
    if (imguiBeginScrollArea("Obj menu", renderer.width - 250 - 10,
                             renderer.height - 10 - 205 - 5 - 390, 250, 205, &objScroll))
        gui.mouseOverMenu = true;
    if (imguiCheck("Show Obj", showObj))
        showObj = !showObj;
    imguiLabel("Load Obj file");
    if (imguiButton(loadObjName.c_str(), objToLoad.empty())) {
        char *fileName = openLoadObjFileDialog(loadObjName.data());
        if (fileName) {
            setLastLoadFileName(fileName);
            objLoaded = false;
            objToLoad = fileName;
            loadObj = true;
        }
    }
    imguiLabel("Save Obj file");
    if (imguiButton(saveObjName.c_str(), objLoaded)) {
        char *fileName = openSaveObjFileDialog(loadObjName.data());
        if (fileName) {
            loadObjName = fileName;
            setLastSaveFileName(fileName);
            saveObjMesh(lastObjFileName.data(), lastSaveObjFileName.data());
            saveObjName = loadObjName;
        }
    }
    imguiSeparatorLine();
    char polyText[64];
    char voxelText[64];
    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    if (recastAdapter.inputGeom && objToLoad.empty() && objLoaded) {
        snprintf(polyText, 64, "Verts: %.1fk  Tris: %.1fk",
                 recastAdapter.getVertCount() / 1000.0f,
                 recastAdapter.getTriCount() / 1000.0f);
        std::pair<int, int> gridSize = recastAdapter.getGridSize();
        snprintf(voxelText, 64, "Voxels: %d x %d", gridSize.first, gridSize.second);
    } else {
        snprintf(polyText, 64, "Verts: 0  Tris: 0");
        snprintf(voxelText, 64, "Voxels: 0 x 0");
    }
    imguiValue(polyText);
    imguiValue(voxelText);
    imguiEndScrollArea();
}

void Obj::finalizeLoad() {
    if (loadObj) {
        lastObjFileName = objToLoad;
        std::string msg = "Loading Obj file: '";
        msg += objToLoad;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        std::thread loadObjThread(&Obj::loadObjMesh, this);
        loadObjThread.detach();
        loadObj = false;
    }

    if (!objLoadDone.empty()) {
        RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        if (recastAdapter.inputGeom) {
            recastAdapter.handleMeshChanged();
            objLoaded = true;
        }
        objLoadDone.clear();
    }
}
