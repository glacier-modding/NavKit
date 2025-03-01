#include "../../include/NavKit/module/Navp.h"
#include <numbers>

#include <filesystem>
#include <fstream>

#include <GL/glut.h>
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/GameConnection.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavWeakness/NavPower.h"
#include "../../include/NavWeakness/NavWeakness.h"
#include "../../include/RecastDemo/imgui.h"
#include "../../include/ResourceLib_HM3/ResourceConverter.h"
#include "../../include/ResourceLib_HM3/ResourceLib.h"
#include "../../include/ResourceLib_HM3/Generated/ZHMGen.h"

#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Obj.h"

Navp::Navp() {
    loadNavpName = "Load Navp";
    lastLoadNavpFile = loadNavpName;
    saveNavpName = "Save Navp";
    lastSaveNavpFile = saveNavpName;
    navpLoaded = false;
    showNavp = true;
    showNavpIndices = false;
    navMesh = new NavPower::NavMesh();
    selectedNavpAreaIndex = -1;
    navpScroll = 0;
    bBoxPosX = 0.0;
    bBoxPosY = 0.0;
    bBoxPosZ = 0.0;
    bBoxSizeX = 100.0;
    bBoxSizeY = 100.0;
    bBoxSizeZ = 100.0;
    lastBBoxPosX = 0.0;
    lastBBoxPosY = 0.0;
    lastBBoxPosZ = 0.0;
    lastBBoxSizeX = 100.0;
    lastBBoxSizeY = 100.0;
    lastBBoxSizeZ = 100.0;
    stairsCheckboxValue = false;
    loading = false;
}

Navp::~Navp() = default;

void Navp::resetDefaults() {
    bBoxPosX = 0.0;
    bBoxPosY = 0.0;
    bBoxPosZ = 0.0;
    bBoxSizeX = 100.0;
    bBoxSizeY = 100.0;
    bBoxSizeZ = 100.0;
    lastBBoxPosX = 0.0;
    lastBBoxPosY = 0.0;
    lastBBoxPosZ = 0.0;
    lastBBoxSizeX = 100.0;
    lastBBoxSizeY = 100.0;
    lastBBoxSizeZ = 100.0;
}

char *Navp::openLoadNavpFileDialog(char *lastNavpFolder) {
    nfdu8filteritem_t filters[2] = {{"Navp files", "navp"}, {"Navp.json files", "navp.json"}};
    return FileUtil::openNfdLoadDialog(filters, 2, lastNavpFolder);
}

char *Navp::openSaveNavpFileDialog(char *lastNavpFolder) {
    nfdu8filteritem_t filters[2] = {{"Navp files", "navp"}, {"Navp.json files", "navp.json"}};
    return FileUtil::openNfdSaveDialog(filters, 2, "output", lastNavpFolder);
}

bool Navp::areaIsStairs(NavPower::Area area) {
    return area.m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_STEPS;
}

void Navp::renderArea(NavPower::Area area, bool selected, int areaIndex) {
    if (areaIsStairs(area)) {
        if (!selected) {
            glColor4f(0.5, 0.5, 0.0, 1.0);
        } else {
            glColor4f(1.0, 1.0, 0.5, 1.0);
        }
    } else {
        if (!selected) {
            glColor4f(0.0, 0.5, 0.0, 1.0);
        } else {
            glColor4f(0.0, 0.5, 0.5, 1.0);
        }
    }
    glBegin(GL_POLYGON);
    for (auto vertex: area.m_edges) {
        glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
    }
    glEnd();
    glBegin(GL_LINES);
    bool previousWasPortal = false;
    bool isFirstVertex = true;
    Vec3 firstVertex{area.m_edges[0]->m_pos.X, area.m_edges[0]->m_pos.Z + 0.01f, -area.m_edges[0]->m_pos.Y};
    for (auto vertex: area.m_edges) {
        if (!isFirstVertex) {
            if (previousWasPortal) {
                glColor3f(1.0, 1.0, 1.0);
            } else {
                if (!selected) {
                    glColor3f(0.0, 1.0, 0.0);
                } else {
                    glColor3f(0.0, 1.0, 1.0);
                }
            }
            glVertex3f(vertex->m_pos.X, vertex->m_pos.Z + 0.01f, -vertex->m_pos.Y);
        } else {
            isFirstVertex = false;
        }
        if (vertex->GetType() == NavPower::EdgeType::EDGE_PORTAL) {
            glColor3f(1.0, 1.0, 1.0);
        } else {
            if (!selected) {
                glColor3f(0.0, 1.0, 0.0);
            } else {
                glColor3f(0.0, 1.0, 1.0);
            }
        }
        previousWasPortal = vertex->GetType() == NavPower::EdgeType::EDGE_PORTAL;
        glVertex3f(vertex->m_pos.X, vertex->m_pos.Z + 0.01f, -vertex->m_pos.Y);
    }
    glVertex3f(firstVertex.X, firstVertex.Y, firstVertex.Z);
    glEnd();

    // Render Portal vertices as line loop
    glColor3f(1.0, 1.0, 1.0);
    for (auto vertex: area.m_edges) {
        const float r = 0.05f;
        if (vertex->GetType() == NavPower::EdgeType::EDGE_PORTAL) {
            glBegin(GL_LINE_LOOP);
            for (int i = 0; i < 8; i++) {
                const float a = (float) i / 8.0f * std::numbers::pi * 2;
                const float fx = (float) vertex->m_pos.X + cosf(a) * r;
                const float fy = (float) vertex->m_pos.Y + sinf(a) * r;
                const float fz = (float) vertex->m_pos.Z;
                glVertex3f(fx, fz, -fy);
            }
            glEnd();
        }
    }
}

void Navp::setSelectedNavpAreaIndex(int index) {
    if (index == -1 && selectedNavpAreaIndex != -1) {
        Logger::log(NK_INFO, ("Deselected area: " + std::to_string(selectedNavpAreaIndex + 1)).c_str());
    }
    selectedNavpAreaIndex = index;
    if (index != -1 && index < navMesh->m_areas.size()) {
        Logger::log(NK_INFO, ("Selected area: " + std::to_string(index + 1)).c_str());
        Vec3 pos = navMesh->m_areas[index].m_area->m_pos;
        char v[16];
        snprintf(v, sizeof(v), "%.2f", pos.X);
        std::string msg = "Area " + std::to_string(index + 1) + ": pos: (";
        msg += std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Y);
        msg += ", " + std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Z);
        msg += ", " + std::string{v} + ") Edges: [";
        int edgeIndex = 0;
        for (auto vertex: navMesh->m_areas[index].m_edges) {
            msg += std::to_string(edgeIndex + 1) + ": (";
            snprintf(v, sizeof(v), "%.2f", vertex->m_pos.X);
            msg += std::string{v};
            snprintf(v, sizeof(v), "%.2f", vertex->m_pos.Y);
            msg += ", " + std::string{v};
            snprintf(v, sizeof(v), "%.2f", vertex->m_pos.Z);
            msg += ", " + std::string{v} + ") ";
            edgeIndex++;
        }
        msg += "]";
        Logger::log(NK_INFO, (msg).c_str());
    }
}

void Navp::renderNavMesh() {
    if (!loading) {
        int areaIndex = 0;
        for (const NavPower::Area &area: navMesh->m_areas) {
            renderArea(area, areaIndex == selectedNavpAreaIndex, areaIndex);
            areaIndex++;
        }
        Vec3 colorRed = {1.0f, 0.4, 0.4f};
        Vec3 colorGreen = {.7f, 1, .7f};
        Vec3 colorBlue = {.7f, .7f, 1.0};
        if (showNavpIndices) {
            areaIndex = 0;
            Renderer& renderer = Renderer::getInstance();
            for (const NavPower::Area &area: navMesh->m_areas) {
                renderer.drawText(std::to_string(areaIndex + 1), {
                                       area.m_area->m_pos.X, area.m_area->m_pos.Z + 0.1f, -area.m_area->m_pos.Y
                                   }, colorBlue);
                if (selectedNavpAreaIndex == areaIndex) {
                    int edgeIndex = 0;
                    for (auto vertex: area.m_edges) {
                        renderer.drawText(std::to_string(edgeIndex + 1),
                                           {vertex->m_pos.X, vertex->m_pos.Z + 0.1f, -vertex->m_pos.Y},
                                           vertex->GetType() == NavPower::EdgeType::EDGE_PORTAL
                                               ? colorRed
                                               : colorGreen);
                        edgeIndex++;
                    }
                }
                areaIndex++;
            }
        }
    }
}

void Navp::renderNavMeshForHitTest() {
    if (!loading && showNavp) {
        int areaIndex = 0;
        for (const NavPower::Area &area: navMesh->m_areas) {
            glColor3ub(60, areaIndex / 255, areaIndex % 255);
            areaIndex++;
            glBegin(GL_POLYGON);
            for (auto vertex: area.m_edges) {
                glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
            }
            glEnd();
        }
    }
}

void Navp::loadNavMeshFileData(Navp *navp, char *fileName) {
    if (!std::filesystem::is_regular_file(fileName))
        throw std::runtime_error("Input path is not a regular file.");

    // Read the entire file to memory.
    const long s_FileSize = std::filesystem::file_size(fileName);

    if (s_FileSize < sizeof(NavPower::NavMesh))
        throw std::runtime_error("Invalid NavMesh File.");

    std::ifstream s_FileStream(fileName, std::ios::in | std::ios::binary);

    if (!s_FileStream)
        throw std::runtime_error("Error creating input file stream.");

    void *s_FileData = malloc(s_FileSize);
    s_FileStream.read(static_cast<char *>(s_FileData), s_FileSize);

    s_FileStream.close();

    navp->navMeshFileData = s_FileData;
}

void Navp::loadNavMesh(Navp *navp, char *fileName, bool isFromJson) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Navp from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();
    navp->loading = true;
    try {
        NavPower::NavMesh newNavMesh = isFromJson ? LoadNavMeshFromJson(fileName) : LoadNavMeshFromBinary(fileName);
        std::swap(*navp->navMesh, newNavMesh);
        if (isFromJson) {
            OutputNavMesh_NAVP_Write(navp->navMesh, navp->outputNavpFilename.data());
            loadNavMeshFileData(navp, navp->outputNavpFilename.data());
        } else {
            loadNavMeshFileData(navp, fileName);
        }
    } catch (...) {
        msg = "Invalid Navp file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_ERROR, msg.data());
        return;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished loading Navp in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.data());
    navp->setSelectedNavpAreaIndex(-1);
    navp->loading = false;
    navp->navpLoaded = true;
    navp->buildBinaryAreaToAreaMap();
}

void Navp::buildNavp(Navp *navp) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Building Navp at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();
    RecastAdapter& recastAdapter = RecastAdapter::getInstance();
    if (recastAdapter.handleBuild()) {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        navp->navpBuildDone.push_back(true);
        msg = "Finished building Navp in ";
        msg += std::to_string(duration.count());
        msg += " seconds";
        Logger::log(NK_INFO, msg.data());
        navp->setSelectedNavpAreaIndex(-1);
    } else {
        Logger::log(NK_ERROR, "Error building Navp");
    }
}

void Navp::setLastLoadFileName(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadNavpName = fileName;
        lastLoadNavpFile = loadNavpName.data();
        loadNavpName = loadNavpName.substr(loadNavpName.find_last_of("/\\") + 1);
        Settings::setValue("Paths", "loadnavp", fileName);
        Settings::save();
    }
}

void Navp::setLastSaveFileName(const char *fileName) {
    saveNavpName = fileName;
    lastSaveNavpFile = saveNavpName.data();
    saveNavpName = saveNavpName.substr(saveNavpName.find_last_of("/\\") + 1);
    Settings::setValue("Paths", "savenavp", fileName);
    Settings::save();
}

void Navp::drawMenu() {
    Renderer& renderer = Renderer::getInstance();
    Gui& gui = Gui::getInstance();
    int navpMenuHeight = std::min(1200, renderer.height - 20);
    if (imguiBeginScrollArea("Navp menu", 10, renderer.height - navpMenuHeight - 10, 250, navpMenuHeight,
                             &navpScroll))
        gui.mouseOverMenu = true;
    if (imguiCheck("Show Navp", showNavp))
        showNavp = !showNavp;
    if (imguiCheck("Show Navp Indices", showNavpIndices))
        showNavpIndices = !showNavpIndices;
    imguiLabel("Load Navp from file");
    if (imguiButton(loadNavpName.c_str(), navpLoadDone.empty())) {
        char *fileName = openLoadNavpFileDialog(lastLoadNavpFile.data());
        if (fileName) {
            setLastLoadFileName(fileName);
            std::string extension = loadNavpName.substr(loadNavpName.length() - 4, loadNavpName.length());
            std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

            if (extension == "JSON") {
                navpLoaded = true;
                std::string msg = "Loading Navp.json file: '";
                msg += fileName;
                msg += "'...";
                Logger::log(NK_INFO, msg.data());
                std::thread loadNavMeshThread(&Navp::loadNavMesh, this, lastLoadNavpFile.data(), true);
                loadNavMeshThread.detach();
            } else if (extension == "NAVP") {
                navpLoaded = true;
                std::string msg = "Loading Navp file: '";
                msg += fileName;
                msg += "'...";
                Logger::log(NK_INFO, msg.data());
                std::thread loadNavMeshThread(&Navp::loadNavMesh, this, lastLoadNavpFile.data(), false);
                loadNavMeshThread.detach();
            }
        }
    }
    imguiLabel("Save Navp to file");
    if (imguiButton(saveNavpName.c_str(), navpLoaded)) {
        char *fileName = openSaveNavpFileDialog(lastLoadNavpFile.data());
        if (fileName) {
            setLastSaveFileName(fileName);
            std::string extension = saveNavpName.substr(saveNavpName.length() - 4, saveNavpName.length());
            std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
            std::string msg = "Saving Navp";
            if (extension == "JSON") {
                msg += ".Json";
            }
            msg += " file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            if (extension == "JSON") {
                OutputNavMesh_JSON_Write(navMesh, lastSaveNavpFile.data());
            } else if (extension == "NAVP") {
                OutputNavMesh_NAVP_Write(navMesh, lastSaveNavpFile.data());
            }
            Logger::log(NK_INFO, "Done saving Navp.");
        }
    }
    if (imguiButton("Send Navp to game", navpLoaded)) {
        GameConnection &gameConnection = GameConnection::getInstance();
        gameConnection.SendNavp(navMesh);
        gameConnection.HandleMessages();
    }
    RecastAdapter& recastAdapter = RecastAdapter::getInstance();
    Obj& obj = Obj::getInstance();
    if (imguiButton("Build Navp from Obj", navpBuildDone.empty() && recastAdapter.inputGeom && obj.objLoaded)) {
        std::thread buildNavpThread(&Navp::buildNavp, this);
        buildNavpThread.detach();
    }
    imguiSeparatorLine();
    imguiLabel("Selected Area");
    char selectedNavpText[64];
    snprintf(selectedNavpText, 64, selectedNavpAreaIndex != -1 ? "Area Index: %d" : "Area Index: None",
             selectedNavpAreaIndex + 1);
    imguiValue(selectedNavpText);

    if (imguiCheck("Stairs",
                   selectedNavpAreaIndex == -1
                       ? false
                       : (selectedNavpAreaIndex < navMesh->m_areas.size()
                              ? areaIsStairs(navMesh->m_areas[selectedNavpAreaIndex])
                              : false),
                   selectedNavpAreaIndex != -1)) {
        if (selectedNavpAreaIndex != -1) {
            NavPower::AreaUsageFlags newType = (navMesh->m_areas[selectedNavpAreaIndex].m_area->m_usageFlags ==
                                                NavPower::AreaUsageFlags::AREA_STEPS)
                                                   ? NavPower::AreaUsageFlags::AREA_FLAT
                                                   : NavPower::AreaUsageFlags::AREA_STEPS;
            std::string newTypeString = (navMesh->m_areas[selectedNavpAreaIndex].m_area->m_usageFlags ==
                                         NavPower::AreaUsageFlags::AREA_STEPS)
                                            ? "AREA_FLAT"
                                            : "AREA_STEPS";
            Logger::log(NK_INFO, ("Setting area type to: " + newTypeString).c_str());
            navMesh->m_areas[selectedNavpAreaIndex].m_area->m_usageFlags = newType;
        }
    }

    imguiSeparatorLine();

    recastAdapter.handleCommonSettings();

    imguiSeparatorLine();
    imguiLabel("Bounding Box");

    bool bboxChanged = false;
    if (imguiSlider("Bounding Box Origin X", &bBoxPosX, -500.0f, 500.0f, 1.0f)) {
        if (lastBBoxPosX != bBoxPosX) {
            bboxChanged = true;
            lastBBoxPosX = bBoxPosX;
        }
    }
    if (imguiSlider("Bounding Box Origin Y", &bBoxPosY, -500.0f, 500.0f, 1.0f)) {
        if (lastBBoxPosY != bBoxPosY) {
            bboxChanged = true;
            lastBBoxPosY = bBoxPosY;
        }
    }
    if (imguiSlider("Bounding Box Origin Z", &bBoxPosZ, -500.0f, 500.0f, 1.0f)) {
        if (lastBBoxPosZ != bBoxPosZ) {
            bboxChanged = true;
            lastBBoxPosZ = bBoxPosZ;
        }
    }
    if (imguiSlider("Bounding Box Size X", &bBoxSizeX, 1.0f, 800.0f, 1.0f)) {
        if (lastBBoxSizeX != bBoxSizeX) {
            bboxChanged = true;
            lastBBoxSizeX = bBoxSizeX;
        }
    }
    if (imguiSlider("Bounding Box Size Y", &bBoxSizeY, 1.0f, 800.0f, 1.0f)) {
        if (lastBBoxSizeY != bBoxSizeY) {
            bboxChanged = true;
            lastBBoxSizeY = bBoxSizeY;
        }
    }
    if (imguiSlider("Bounding Box Size Z", &bBoxSizeZ, 1.0f, 800.0f, 1.0f)) {
        if (lastBBoxSizeZ != bBoxSizeZ) {
            bboxChanged = true;
            lastBBoxSizeZ = bBoxSizeZ;
        }
    }
    if (imguiButton("Reset Defaults")) {
        resetDefaults();
        recastAdapter.resetCommonSettings();
        float bBoxPos[3] = {bBoxPosX, bBoxPosY, bBoxPosZ};
        float bBoxSize[3] = {bBoxSizeX, bBoxSizeY, bBoxSizeZ};
        obj.setBBox(bBoxPos, bBoxSize);
    }

    if (bboxChanged) {
        float bBoxPos[3] = {bBoxPosX, bBoxPosY, bBoxPosZ};
        float bBoxSize[3] = {bBoxSizeX, bBoxSizeY, bBoxSizeZ};
        obj.setBBox(bBoxPos, bBoxSize);
    }
    imguiEndScrollArea();
}

void Navp::buildBinaryAreaToAreaMap() {
    binaryAreaToAreaMap.clear();
    for (NavPower::Area &area: navMesh->m_areas) {
        binaryAreaToAreaMap.emplace(area.m_area, &area);
    }
}

void Navp::finalizeLoad() {
    if (!navpBuildDone.empty()) {
        navpLoaded = true;
        const RecastAdapter& recastAdapter = RecastAdapter::getInstance();
        recastAdapter.save(outputNavpFilename.data());
        std::thread loadNavMeshThread(&Navp::loadNavMesh, this, outputNavpFilename.data(), true);
        loadNavMeshThread.detach();
        navpBuildDone.clear();
        buildBinaryAreaToAreaMap();
    }
}
