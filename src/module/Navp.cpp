#include "../../include/NavKit/module/Navp.h"
#include <numbers>

#include <filesystem>
#include <fstream>

#include <cpptrace/from_current.hpp>
#include <GL/glew.h>
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavWeakness/NavPower.h"
#include "../../include/NavWeakness/NavWeakness.h"
#include "../../include/RecastDemo/InputGeom.h"
#include "../../include/ResourceLib_HM3/ResourceConverter.h"
#include "../../include/ResourceLib_HM3/ResourceLib.h"
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"


Navp::Navp()
    : navMesh(new NavPower::NavMesh()),
      selectedNavpAreaIndex(-1),
      selectedPfSeedPointIndex(-1),
      selectedExclusionBoxIndex(-1),
      navpLoaded(false),
      showNavp(true),
      showNavpIndices(false),
      showPfExclusionBoxes(true),
      showPfSeedPoints(true),
      showRecastDebugInfo(false),
      doNavpHitTest(false),
      doNavpExclusionBoxHitTest(false),
      doNavpPfSeedPointHitTest(false),
      loading(false),
      stairsCheckboxValue(false),
      building(false),
      loadNavpName("Load Navp"),
      lastLoadNavpFile(loadNavpName),
      saveNavpName("Save Navp"),
      lastSaveNavpFile(saveNavpName) {
}

Navp::~Navp() = default;

void Navp::renderPfSeedPoints() const {
    if (showPfSeedPoints) {
        Renderer &renderer = Renderer::getInstance();
        int i = 0;
        for (const ZPathfinding::PfSeedPoint &seedPoint: Scene::getInstance().pfSeedPoints) {
            Vec3 fill = {0, 0, 0.6};
            Vec3 outline = {0, 0, 0.8};
            if (selectedPfSeedPointIndex == i) {
                fill = {0, 0, 0.8};
                outline = {0, 0, 1.0};
            }
            renderer.drawBox(
                {seedPoint.pos.x, seedPoint.pos.z + 0.5f, -seedPoint.pos.y},
                {0.25, 0.5, 0.25},
                {0, 0, 0, 1},
                true,
                fill,
                true,
                outline,
                1.0);
            i++;
        }
    }
}

void Navp::renderPfSeedPointsForHitTest() const {
    if (showPfSeedPoints && Scene::getInstance().sceneLoaded) {
        Renderer &renderer = Renderer::getInstance();
        int i = 0;
        for (const ZPathfinding::PfSeedPoint &seedPoint: Scene::getInstance().pfSeedPoints) {
            const int highByte = (i >> 8) & 0xFF;
            const int lowByte = i & 0xFF;
            renderer.drawBox(
                {seedPoint.pos.x, seedPoint.pos.z + 0.5f, -seedPoint.pos.y},
                {0.25, 0.5, 0.25},
                {0, 0, 0, 1},
                true,
                {static_cast<float>(PF_SEED_POINT) / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                true,
                {static_cast<float>(PF_SEED_POINT) / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                1.0);
            i++;
        }
    }
}

void Navp::renderExclusionBoxes() const {
    if (showPfExclusionBoxes) {
        Renderer &renderer = Renderer::getInstance();
        int i = 0;
        for (const ZPathfinding::PfBox &exclusionBox: Scene::getInstance().exclusionBoxes) {
            Vec3 fill = {0.6, 0, 0};
            Vec3 outline = {0.8, 0, 0};
            if (selectedExclusionBoxIndex == i) {
                fill = {0.8, 0, 0};
                outline = {1.0, 0, 0};
            }
            renderer.drawBox(
                {exclusionBox.pos.x, exclusionBox.pos.z, -exclusionBox.pos.y},
                {exclusionBox.scale.x, exclusionBox.scale.z, -exclusionBox.scale.y},
                {exclusionBox.rotation.x, exclusionBox.rotation.z, -exclusionBox.rotation.y, exclusionBox.rotation.w},
                true,
                fill,
                true,
                outline,
                1.0);
            i++;
        }
    }
}

void Navp::renderExclusionBoxesForHitTest() const {
    if (showPfExclusionBoxes && Scene::getInstance().sceneLoaded) {
        Renderer &renderer = Renderer::getInstance();
        int i = 0;
        for (const ZPathfinding::PfBox &exclusionBox: Scene::getInstance().exclusionBoxes) {
            const int highByte = (i >> 8) & 0xFF;
            const int lowByte = i & 0xFF;
            renderer.drawBox(
                {exclusionBox.pos.x, exclusionBox.pos.z, -exclusionBox.pos.y},
                {exclusionBox.scale.x, exclusionBox.scale.z, -exclusionBox.scale.y},
                {exclusionBox.rotation.x, exclusionBox.rotation.z, -exclusionBox.rotation.y, exclusionBox.rotation.w},
                true,
                {static_cast<float>(PF_EXCLUSION_BOX) / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                true,
                {static_cast<float>(PF_EXCLUSION_BOX) / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                1.0);
            i++;
        }
    }
}

char *Navp::openLoadNavpFileDialog() {
    nfdu8filteritem_t filters[2] = {{"Navp files", "navp"}, {"Navp.json files", "navp.json"}};
    return FileUtil::openNfdLoadDialog(filters, 2);
}

char *Navp::openSaveNavpFileDialog() {
    nfdu8filteritem_t filters[2] = {{"Navp files", "navp"}, {"Navp.json files", "navp.json"}};
    return FileUtil::openNfdSaveDialog(filters, 2, "output");
}

void Navp::updateExclusionBoxConvexVolumes() {
    if (Obj::getInstance().objLoaded) {
        if (const RecastAdapter &recastAdapter = RecastAdapter::getInstance(); recastAdapter.inputGeom) {
            recastAdapter.clearConvexVolumes();
            for (ZPathfinding::PfBox exclusionBox: Scene::getInstance().exclusionBoxes) {
                recastAdapter.addConvexVolume(exclusionBox);
            }
        }
    }
}

bool Navp::areaIsStairs(const NavPower::Area &area) {
    return area.m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_STEPS;
}

void Navp::setStairsFlags() const {
    for (auto &area: navMesh->m_areas) {
        const Vec3 normal = area.CalculateNormal();
        const Vec3 horizontalProjection = {normal.Y, 0.0f, normal.Z};
        const float horizontalMagnitude = std::sqrt(
            horizontalProjection.X * horizontalProjection.X + horizontalProjection.Z * horizontalProjection.Z);
        if (horizontalMagnitude == 0.0f) {
            continue;
        }
        const float dotProduct = normal.X * horizontalProjection.X + normal.Z * horizontalProjection.Z;
        const float normalMagnitude = std::sqrt(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
        float cosineAngle = dotProduct / (normalMagnitude * horizontalMagnitude);
        cosineAngle = std::clamp(cosineAngle, -1.0f, 1.0f);
        const float angleRadians = std::acos(cosineAngle);
        if (const float angleDegrees = angleRadians * 180.0f / std::numbers::pi_v<float>; angleDegrees >= 18.0f) {
            area.m_area->m_usageFlags = NavPower::AreaUsageFlags::AREA_STEPS;
        }
    }
}

void Navp::renderArea(const NavPower::Area &area, const bool selected) {
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
            glColor4f(0.0, 0.9, 0.0, 1.0);
        }
    }
    glBegin(GL_POLYGON);
    for (const auto vertex: area.m_edges) {
        glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
    }
    glEnd();
    glBegin(GL_LINES);
    bool previousWasPortal = false;
    bool isFirstVertex = true;
    const Vec3 firstVertex{area.m_edges[0]->m_pos.X, area.m_edges[0]->m_pos.Z + 0.01f, -area.m_edges[0]->m_pos.Y};
    for (const auto vertex: area.m_edges) {
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

void Navp::setSelectedNavpAreaIndex(const int index) {
    if (index == -1 && selectedNavpAreaIndex != -1) {
        Logger::log(NK_INFO, ("Deselected area: " + std::to_string(selectedNavpAreaIndex + 1)).c_str());
    }
    selectedNavpAreaIndex = index;
    if (index != -1 && index < navMesh->m_areas.size()) {
        auto &selectedArea = navMesh->m_areas[index];
        Logger::log(NK_INFO, ("Selected area: " + std::to_string(index + 1)).c_str());
        Vec3 pos = selectedArea.m_area->m_pos;
        char v[16];
        snprintf(v, sizeof(v), "%.2f", pos.X);
        std::string msg = "Area " + std::to_string(index + 1) + ": pos: (";
        msg += std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Y);
        msg += ", " + std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Z);
        msg += ", " + std::string{v} + ") Edges: [";
        int edgeIndex = 0;
        for (const auto vertex: selectedArea.m_edges) {
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
        const Vec3 normal = selectedArea.CalculateNormal();
        const Vec3 horizontalProjection = {normal.Y, 0.0f, normal.Z};
        const float horizontalMagnitude = std::sqrt(
            horizontalProjection.X * horizontalProjection.X + horizontalProjection.Z * horizontalProjection.Z);
        float angleDegrees = 0;
        if (horizontalMagnitude == 0.0f) {
            angleDegrees = 90;
        } else {
            const float dotProduct = normal.X * horizontalProjection.X + normal.Z * horizontalProjection.Z;
            const float normalMagnitude = std::sqrt(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
            float cosineAngle = dotProduct / (normalMagnitude * horizontalMagnitude);
            cosineAngle = std::clamp(cosineAngle, -1.0f, 1.0f);
            const float angleRadians = std::acos(cosineAngle);
            angleDegrees = angleRadians * 180.0f / M_PI;
        }
        msg += " Angle: " + std::to_string(angleDegrees);
        Logger::log(NK_INFO, (msg).c_str());
    }
    Menu::updateMenuState();
}

void Navp::setSelectedPfSeedPointIndex(const int index) {
    const Scene &scene = Scene::getInstance();

    if (index == -1 && selectedPfSeedPointIndex != -1) {
        auto &selectedPFSeedPoint = scene.pfSeedPoints[selectedPfSeedPointIndex];
        Logger::log(
            NK_INFO,
            ("Deselected PF Seed point: name: '" + selectedPFSeedPoint.name + "' id: '" + selectedPFSeedPoint.id + "'").
            c_str());
    }
    selectedPfSeedPointIndex = index;
    if (index != -1 && index < scene.pfSeedPoints.size()) {
        auto &selectedPFSeedPoint = scene.pfSeedPoints[index];
        Logger::log(
            NK_INFO,
            ("Selected PF Seed point: name: '" + selectedPFSeedPoint.name + "' id: '" + selectedPFSeedPoint.id + "'").
            c_str());
        Vec3 pos = {selectedPFSeedPoint.pos.x, selectedPFSeedPoint.pos.y, selectedPFSeedPoint.pos.z};
        char v[16];
        snprintf(v, sizeof(v), "%.2f", pos.X);
        std::string msg = "PF Seed point name: '" + selectedPFSeedPoint.name + "' id: '" + selectedPFSeedPoint.id +
                          "' pos: (";
        msg += std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Y);
        msg += ", " + std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Z);
        msg += ", " + std::string{v} + ")";
        Logger::log(NK_INFO, (msg).c_str());
    }
}

void Navp::setSelectedExclusionBoxIndex(int index) {
    Scene &scene = Scene::getInstance();

    if (index == -1 && selectedExclusionBoxIndex != -1) {
        auto &selectedExclusionBox = scene.exclusionBoxes[selectedExclusionBoxIndex];
        Logger::log(
            NK_INFO,
            ("Deselected Exclusion Box: name '" + selectedExclusionBox.name + "' id: '" + selectedExclusionBox.id + "'")
            .c_str());
    }
    selectedExclusionBoxIndex = index;
    if (index != -1 && index < scene.exclusionBoxes.size()) {
        auto &selectedExclusionBox = scene.exclusionBoxes[index];

        Logger::log(
            NK_INFO,
            ("Selected Exclusion Box: name '" + selectedExclusionBox.name + "' id: '" + selectedExclusionBox.id + "'").
            c_str());
        Vec3 pos = {selectedExclusionBox.pos.x, selectedExclusionBox.pos.y, selectedExclusionBox.pos.z};
        char v[16];
        snprintf(v, sizeof(v), "%.2f", pos.X);
        std::string msg =
                "Exclusion Box name '" + selectedExclusionBox.name + "' id: '" + selectedExclusionBox.id + "' pos: (";
        msg += std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Y);
        msg += ", " + std::string{v};
        snprintf(v, sizeof(v), "%.2f", pos.Z);
        msg += ", " + std::string{v} + ")";
        Logger::log(NK_INFO, (msg).c_str());
    }
}

void Navp::renderNavMesh() {
    if (!loading) {
        int areaIndex = 0;
        for (const NavPower::Area &area: navMesh->m_areas) {
            renderArea(area, areaIndex == selectedNavpAreaIndex);
            areaIndex++;
        }
        Vec3 colorRed = {1.0f, 0.4f, 0.4f};
        Vec3 colorGreen = {.7f, 1.0f, .7f};
        Vec3 colorBlue = {.7f, .7f, 1.0f};
        Vec3 colorPink = {1.0f, .7f, 1.0f};
        if (showNavpIndices) {
            areaIndex = 0;
            Renderer &renderer = Renderer::getInstance();
            for (const NavPower::Area &area: navMesh->m_areas) {
                renderer.drawText(std::to_string(areaIndex + 1), {
                                      area.m_area->m_pos.X, area.m_area->m_pos.Z + 0.1f, -area.m_area->m_pos.Y
                                  }, colorBlue);
                if (selectedNavpAreaIndex == areaIndex) {
                    int edgeIndex = 0;
                    for (const auto vertex: area.m_edges) {
                        renderer.drawText(std::to_string(edgeIndex + 1),
                                          {vertex->m_pos.X, vertex->m_pos.Z + 0.1f, -vertex->m_pos.Y},
                                          vertex->GetType() == NavPower::EdgeType::EDGE_PORTAL
                                              ? colorRed
                                              : colorGreen);
                        if (vertex->m_pAdjArea != 0) {
                            const auto nextVertex = area.m_edges[(edgeIndex + 1) % area.m_edges.size()];
                            Vec3 midpoint = (vertex->m_pos + nextVertex->m_pos) / 2.0f;
                            const int neighborAreaIndex = binaryAreaToAreaIndexMap[vertex->m_pAdjArea];
                            renderer.drawText(std::to_string(neighborAreaIndex),
                                              {midpoint.X, midpoint.Z + 0.1f, -midpoint.Y},
                                              colorPink);
                        }
                        edgeIndex++;
                    }
                }
                areaIndex++;
            }
        }
    }
}

void Navp::renderNavMeshForHitTest() const {
    if (!loading && showNavp && navpLoaded) {
        int areaIndex = 0;
        for (const NavPower::Area &area: navMesh->m_areas) {
            glColor3ub(NAVMESH_AREA, areaIndex / 255, areaIndex % 255);
            areaIndex++;
            glBegin(GL_POLYGON);
            for (auto vertex: area.m_edges) {
                glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
            }
            glEnd();
        }
    }
}

void Navp::loadNavMeshFileData(const std::string &fileName) {
    if (!std::filesystem::is_regular_file(fileName))
        throw std::runtime_error("Input path is not a regular file.");

    const long s_FileSize = std::filesystem::file_size(fileName);

    if (s_FileSize < sizeof(NavPower::NavMesh))
        throw std::runtime_error("Invalid NavMesh File.");

    std::ifstream s_FileStream(fileName, std::ios::in | std::ios::binary);

    if (!s_FileStream)
        throw std::runtime_error("Error creating input file stream.");

    navMeshFileData.resize(s_FileSize);

    s_FileStream.read(navMeshFileData.data(), s_FileSize);
}

void Navp::loadNavMesh(const std::string &fileName, bool isFromJson, bool isFromBuildingNavp, bool isFromBuildingAirg) {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Navp from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.c_str());
    const auto start = std::chrono::high_resolution_clock::now();
    loading = true;
    CPPTRACE_TRY
        {
            NavPower::NavMesh newNavMesh = isFromJson
                                               ? LoadNavMeshFromJson(fileName.c_str())
                                               : LoadNavMeshFromBinary(fileName.c_str());
            std::swap(*navMesh, newNavMesh);
            const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
            if (isFromBuildingNavp || isFromBuildingAirg) {
                setStairsFlags();
                outputNavpFilename = navKitSettings.outputFolder + (isFromBuildingNavp
                                                                  ? "\\output.navp"
                                                                  : "\\outputForAirg.navp");
                OutputNavMesh_JSON_Write(navMesh, (outputNavpFilename + ".json").c_str());
                NavPower::NavMesh reloadedNavMesh = LoadNavMeshFromJson((outputNavpFilename + ".json").c_str());
                std::swap(*navMesh, reloadedNavMesh);
            }
            if (isFromJson) {
                OutputNavMesh_NAVP_Write(navMesh, outputNavpFilename.c_str());
                loadNavMeshFileData(outputNavpFilename);
            } else {
                loadNavMeshFileData(fileName);
            }
        }
    CPPTRACE_CATCH(const std::exception& e) {
        msg = "Error loading Navp file '";
        msg += fileName;
        msg += "': ";
        msg += e.what();
        msg += " Stack trace: ";
        msg += cpptrace::from_current_exception().to_string();
        Logger::log(NK_ERROR, msg.c_str());
        return;
    } catch (...) {
        msg = "Invalid Navp file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_ERROR, msg.c_str());
        return;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished loading Navp in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.c_str());
    setSelectedNavpAreaIndex(-1);
    loading = false;
    navpLoaded = true;
    Menu::updateMenuState();
    buildAreaMaps();
    if (Airg &airg = Airg::getInstance(); SceneExtract::getInstance().alsoBuildAll && airg.canBuildAirg()) {
        airg.handleBuildAirgClicked();
    }
}

void Navp::buildNavp() {
    CPPTRACE_TRY
        {
            std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            std::string msg = "Building Navp at ";
            msg += std::ctime(&start_time);
            Logger::log(NK_INFO, msg.data());
            auto start = std::chrono::high_resolution_clock::now();
            RecastAdapter &recastAdapter = RecastAdapter::getInstance();
            updateExclusionBoxConvexVolumes();
            building = true;
            Menu::updateMenuState();
            Logger::log(NK_INFO, "Beginning Recast build...");
            if (recastAdapter.handleBuild()) {
                Logger::log(NK_INFO, "Done with Recast build.");
                Logger::log(NK_INFO, "Pruning areas unreachable by PF Seed Points.");
                recastAdapter.findPfSeedPointAreas();
                recastAdapter.excludeNonReachableAreas();
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
                navpBuildDone.store(true);
                msg = "Finished building Navp in ";
                msg += std::to_string(duration.count());
                msg += " seconds";
                Logger::log(NK_INFO, msg.data());
                setSelectedNavpAreaIndex(-1);
                Menu::updateMenuState();
            } else {
                Logger::log(NK_ERROR, "Error building Navp");
                building = false;
                navpBuildDone.store(false);
                Menu::updateMenuState();
            }
        }
    CPPTRACE_CATCH(const std::exception& e) {
        std::string msg = "Error building Navp: ";
        msg += e.what();
        msg += " Stack trace: ";
        msg += cpptrace::from_current_exception().to_string();
        Logger::log(NK_ERROR, msg.c_str());
    } catch (...) {
        Logger::log(NK_ERROR, "Error building Navp.");
    }
}

void Navp::setLastLoadFileName(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadNavpName = fileName;
        lastLoadNavpFile = loadNavpName.data();
        loadNavpName = loadNavpName.substr(loadNavpName.find_last_of("/\\") + 1);
    }
}

void Navp::setLastSaveFileName(const char *fileName) {
    saveNavpName = fileName;
    lastSaveNavpFile = saveNavpName.data();
    saveNavpName = saveNavpName.substr(saveNavpName.find_last_of("/\\") + 1);
}

void Navp::handleOpenNavpClicked() {
    char *fileName = openLoadNavpFileDialog();
    if (fileName) {
        setLastLoadFileName(fileName);
        std::string extension = loadNavpName.substr(loadNavpName.length() - 4, loadNavpName.length());
        std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

        if (extension == "JSON") {
            std::string msg = "Loading Navp.json file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            backgroundWorker.emplace(&Navp::loadNavMesh, this, lastLoadNavpFile, true, false, false);
        } else if (extension == "NAVP") {
            std::string msg = "Loading Navp file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            backgroundWorker.emplace(&Navp::loadNavMesh, this, lastLoadNavpFile, false, false, false);
        }
    }
}

void Navp::saveNavMesh(const std::string &fileName, const std::string &extension) {
    loading = true;

    const auto start = std::chrono::high_resolution_clock::now();
    Logger::log(NK_INFO, "Saving Navp to file: '%s'...", fileName.c_str());

    try {
        std::string upper_extension = extension;
        std::ranges::transform(upper_extension, upper_extension.begin(), ::toupper);

        if (upper_extension == "JSON") {
            OutputNavMesh_JSON_Write(navMesh, fileName.c_str());
        } else if (upper_extension == "NAVP") {
            const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
            const std::string tempOutputJSONFilename = navKitSettings.outputFolder + "\\temp_save.navp.json";

            OutputNavMesh_JSON_Write(navMesh, tempOutputJSONFilename.c_str());

            NavPower::NavMesh newNavMesh = LoadNavMeshFromJson(tempOutputJSONFilename.c_str());
            std::swap(*navMesh, newNavMesh);

            OutputNavMesh_NAVP_Write(navMesh, fileName.c_str());
            buildAreaMaps();
        }

        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        Logger::log(NK_INFO, "Finished saving Navp in %lld ms.", duration.count());
    } catch (const std::exception &e) {
        Logger::log(NK_ERROR, "Error saving Navp file '%s': %s", fileName.c_str(), e.what());
    } catch (...) {
        Logger::log(NK_ERROR, "An unknown error occurred while saving Navp file '%s'", fileName.c_str());
    }

    loading = false;
}

void Navp::handleSaveNavpClicked() {
    if (char *fileName = openSaveNavpFileDialog()) {
        setLastSaveFileName(fileName);
        std::string extension = saveNavpName.substr(saveNavpName.length() - 4, saveNavpName.length());

        backgroundWorker.emplace(&Navp::saveNavMesh, this, lastSaveNavpFile, extension);
    }
}

bool Navp::stairsAreaSelected() const {
    return selectedNavpAreaIndex == -1
               ? false
               : selectedNavpAreaIndex < navMesh->m_areas.size()
                     ? areaIsStairs(navMesh->m_areas[selectedNavpAreaIndex])
                     : false;
}

bool Navp::canBuildNavp() const {
    const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    const Obj &obj = Obj::getInstance();
    const Scene &scene = Scene::getInstance();
    return !navpBuildDone && !building && recastAdapter.inputGeom && obj.objLoaded && !
           Airg::getInstance().airgBuilding && scene.sceneLoaded;
}

bool Navp::canSave() const {
    return navpLoaded && !loading && !building;
}

void Navp::handleEditStairsClicked() const {
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
        Menu::updateMenuState();
    }
}

void Navp::handleBuildNavpClicked() {
    backgroundWorker.emplace(&Navp::buildNavp, this);
}

void Navp::buildAreaMaps() {
    binaryAreaToAreaMap.clear();
    posToAreaMap.clear();
    int areaIndex = 1;
    for (NavPower::Area &area: navMesh->m_areas) {
        binaryAreaToAreaIndexMap.emplace(area.m_area, areaIndex);
        binaryAreaToAreaMap.emplace(area.m_area, &area);
        posToAreaMap.emplace(area.m_area->m_pos, &area);
        areaIndex++;
    }
}

void Navp::finalizeBuild() {
    if (navpBuildDone) {
        navpLoaded = true;
        const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
        outputNavpFilename = navKitSettings.outputFolder + "\\output.navp.json";
        recastAdapter.save(outputNavpFilename);
        backgroundWorker.emplace(&Navp::loadNavMesh, this, outputNavpFilename, true, true, false);
        navpBuildDone.store(false);
        building = false;
        Menu::updateMenuState();
    }
}
