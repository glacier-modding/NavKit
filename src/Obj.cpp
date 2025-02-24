#include "..\include\NavKit\Obj.h"

Obj::Obj(NavKit *navKit): navKit(navKit) {
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
    navKit->log(RC_LOG_PROGRESS,
                ("Setting bbox to (" + std::to_string(bBoxPos[0]) + ", " + std::to_string(bBoxPos[1]) + ", " +
                 std::to_string(bBoxPos[2]) + ") (" + std::to_string(bBoxSize[0]) + ", " + std::to_string(bBoxSize[1]) +
                 ", " + std::to_string(bBoxSize[2]) + ")").c_str());
}

void Obj::copyObjFile(NavKit *navKit, const std::string &from, const std::string &to) {
    if (from == to) {
        navKit->log(RC_LOG_ERROR, ("Cannot overwrite current obj file: " + from).c_str());
        return;
    }
    auto start = std::chrono::high_resolution_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    std::string navKitObjProperties = "";
    navKitObjProperties += "# NavKit OBJ Properties:\n";
    navKitObjProperties += "#![BBox]\n";
    navKitObjProperties += "#!x = " + std::to_string(navKit->obj->bBoxPos[0]) + "\n";
    navKitObjProperties += "#!y = " + std::to_string(navKit->obj->bBoxPos[1]) + "\n";
    navKitObjProperties += "#!z = " + std::to_string(navKit->obj->bBoxPos[2]) + "\n";
    navKitObjProperties += "#!sx = " + std::to_string(navKit->obj->bBoxSize[0]) + "\n";
    navKitObjProperties += "#!sy = " + std::to_string(navKit->obj->bBoxSize[1]) + "\n";
    navKitObjProperties += "#!sz = " + std::to_string(navKit->obj->bBoxSize[2]) + "\n";
    std::ifstream inputFile(from);
    if (!inputFile.is_open()) {
        navKit->log(RC_LOG_ERROR, ("Error opening obj file for reading: " + from).c_str());
        return;
    }
    std::ofstream outputFile(to);
    if (!outputFile.is_open()) {
        navKit->log(RC_LOG_ERROR, ("Error opening file for writing: " + to).c_str());
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
    navKit->log(RC_LOG_PROGRESS, msg.data());
}

void Obj::saveObjMesh(char *objToCopy, char *newFileName) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Obj to file at ";
    msg += std::ctime(&start_time);
    navKit->log(RC_LOG_PROGRESS, msg.data());
    std::thread saveObjThread(&Obj::copyObjFile, navKit, objToCopy, newFileName);
    saveObjThread.detach();
}

void Obj::loadObjMesh(Obj *obj) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Obj from file at ";
    msg += std::ctime(&start_time);
    obj->navKit->log(RC_LOG_PROGRESS, msg.data());
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream inputFile(obj->objToLoad);
    if (!inputFile.is_open()) {
        obj->navKit->log(RC_LOG_ERROR, ("Error opening obj file for reading: " + obj->objToLoad).c_str());
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
                            obj->navKit->log(RC_LOG_ERROR,
                                             ("Error converting value to float for property " + propertyName + ": " + e.
                                              what()).c_str());
                            error = true;
                            break;
                        }
                        catch (const std::out_of_range &e) {
                            obj->navKit->log(RC_LOG_ERROR,
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

    if (obj->navKit->geom->load(&obj->navKit->ctx, obj->objToLoad)) {
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
            obj->navKit->log(RC_LOG_PROGRESS, msg.data());
        }
    } else {
        obj->navKit->log(RC_LOG_ERROR, "Error loading obj.");
    }
    obj->objToLoad.clear();
}

void Obj::setBBox(float *pos, float *size) {
    bBoxPos[0] = pos[0];
    navKit->navp->bBoxPosX = pos[0];
    bBoxPos[1] = pos[1];
    navKit->navp->bBoxPosY = pos[1];
    bBoxPos[2] = pos[2];
    navKit->navp->bBoxPosZ = pos[2];
    bBoxSize[0] = size[0];
    navKit->navp->bBoxSizeX = size[0];
    bBoxSize[1] = size[1];
    navKit->navp->bBoxSizeY = size[1];
    bBoxSize[2] = size[2];
    navKit->navp->bBoxSizeZ = size[2];
    if (navKit->geom != nullptr) {
        navKit->geom->m_meshBMin[0] = bBoxPos[0] - bBoxSize[0] / 2;
        navKit->geom->m_meshBMin[1] = bBoxPos[1] - bBoxSize[1] / 2;
        navKit->geom->m_meshBMin[2] = bBoxPos[2] - bBoxSize[2] / 2;
        navKit->geom->m_meshBMax[0] = bBoxPos[0] + bBoxSize[0] / 2;
        navKit->geom->m_meshBMax[1] = bBoxPos[1] + bBoxSize[1] / 2;
        navKit->geom->m_meshBMax[2] = bBoxPos[2] + bBoxSize[2] / 2;
    }
    navKit->log(RC_LOG_PROGRESS,
                ("Setting bbox to (" + std::to_string(pos[0]) + ", " + std::to_string(pos[1]) + ", " +
                 std::to_string(pos[2]) + ") (" + std::to_string(size[0]) + ", " + std::to_string(size[1]) + ", " +
                 std::to_string(size[2]) + ")").c_str());

    navKit->ini.SetValue("BBox", "x", std::to_string(pos[0]).c_str());
    navKit->ini.SetValue("BBox", "y", std::to_string(pos[1]).c_str());
    navKit->ini.SetValue("BBox", "z", std::to_string(pos[2]).c_str());
    navKit->ini.SetValue("BBox", "sx", std::to_string(size[0]).c_str());
    navKit->ini.SetValue("BBox", "sy", std::to_string(size[1]).c_str());
    navKit->ini.SetValue("BBox", "sz", std::to_string(size[2]).c_str());
    navKit->ini.SaveFile("NavKit.ini");
}

void duDebugDrawTriMeshWireframe(duDebugDraw *dd, const float *verts, int /*nverts*/,
                                 const int *tris, const float *normals, int ntris,
                                 const unsigned char *flags, const float texScale) {
    if (!dd) return;
    if (!verts) return;
    if (!tris) return;
    if (!normals) return;

    float uva[2];
    float uvb[2];
    float uvc[2];

    const unsigned int unwalkable = duRGBA(192, 128, 0, 255);


    //dd->begin(DU_DRAW_TRIS);
    for (int i = 0; i < ntris * 3; i += 3) {
        glBegin(GL_LINE_LOOP);
        const float *norm = &normals[i];
        unsigned int color;
        unsigned char a = (unsigned char) (220 * (2 + norm[0] + norm[1]) / 4);
        if (flags && !flags[i / 3])
            color = duLerpCol(duRGBA(a, a, a, 255), unwalkable, 64);
        else
            color = duRGBA(a, a, a, 255);

        const float *va = &verts[tris[i + 0] * 3];
        const float *vb = &verts[tris[i + 1] * 3];
        const float *vc = &verts[tris[i + 2] * 3];

        int ax = 0, ay = 0;
        if (rcAbs(norm[1]) > rcAbs(norm[ax]))
            ax = 1;
        if (rcAbs(norm[2]) > rcAbs(norm[ax]))
            ax = 2;
        ax = (1 << ax) & 3; // +1 mod 3
        ay = (1 << ax) & 3; // +1 mod 3

        uva[0] = va[ax] * texScale;
        uva[1] = va[ay] * texScale;
        uvb[0] = vb[ax] * texScale;
        uvb[1] = vb[ay] * texScale;
        uvc[0] = vc[ax] * texScale;
        uvc[1] = vc[ay] * texScale;

        dd->vertex(va, color, uva);
        dd->vertex(vb, color, uvb);
        dd->vertex(vc, color, uvc);
        dd->end();
    }
}

void Obj::renderObj(InputGeom *m_geom, DebugDrawGL *m_dd) {
    // Draw mesh
    duDebugDrawTriMesh(m_dd, m_geom->getMesh()->getVerts(), m_geom->getMesh()->getVertCount(),
                       m_geom->getMesh()->getTris(), m_geom->getMesh()->getNormals(), m_geom->getMesh()->getTriCount(),
                       0, 1.0f);
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
        navKit->ini.SetValue("Paths", "loadobj", fileName);
        navKit->ini.SaveFile("NavKit.ini");
    }
}

void Obj::setLastSaveFileName(const char *fileName) {
    lastSaveObjFileName = fileName;
    loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
    navKit->ini.SetValue("Paths", "saveobj", fileName);
    navKit->ini.SaveFile("NavKit.ini");
}

void Obj::drawMenu() {
    if (imguiBeginScrollArea("Obj menu", navKit->renderer->width - 250 - 10,
                             navKit->renderer->height - 10 - 205 - 5 - 390, 250, 205, &objScroll))
        navKit->gui->mouseOverMenu = true;
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
    if (navKit->geom && objToLoad.empty() && objLoaded) {
        snprintf(polyText, 64, "Verts: %.1fk  Tris: %.1fk",
                 navKit->geom->getMesh()->getVertCount() / 1000.0f,
                 navKit->geom->getMesh()->getTriCount() / 1000.0f);
        const float *bmin = navKit->sample->m_geom->getNavMeshBoundsMin();
        const float *bmax = navKit->sample->m_geom->getNavMeshBoundsMax();
        int gw = 0, gh = 0;
        rcCalcGridSize(bmin, bmax, navKit->sample->m_cellSize, &gw, &gh);
        snprintf(voxelText, 64, "Voxels: %d x %d", gw, gh);
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
        navKit->geom = new InputGeom();
        lastObjFileName = objToLoad;
        std::string msg = "Loading Obj file: '";
        msg += objToLoad;
        msg += "'...";
        navKit->log(RC_LOG_PROGRESS, msg.data());
        std::thread loadObjThread(&Obj::loadObjMesh, this);
        loadObjThread.detach();
        loadObj = false;
    }

    if (!objLoadDone.empty()) {
        if (navKit->geom) {
            navKit->sample->handleMeshChanged(navKit->geom);
            objLoaded = true;
        }
        objLoadDone.clear();
    }
}
