#include "../../include/NavKit/module/Navp.h"
#include <numbers>

#include <filesystem>
#include <fstream>

#include <cpptrace/from_current.hpp>
#include <GL/glew.h>
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/model/ZPathfinding.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/GameConnection.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/Resource.h"
#include "../../include/NavWeakness/NavPower.h"
#include "../../include/NavWeakness/NavWeakness.h"
#include "../../include/RecastDemo/imgui.h"
#include "../../include/RecastDemo/InputGeom.h"
#include "../../include/ResourceLib_HM3/ResourceConverter.h"
#include "../../include/ResourceLib_HM3/ResourceLib.h"
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"


Navp::Navp()
    : navMesh(new NavPower::NavMesh())
      , selectedNavpAreaIndex(-1)
      , selectedPfSeedPointIndex(-1)
      , selectedExclusionBoxIndex(-1)
      , navpLoaded(false)
      , showNavp(true)
      , showNavpIndices(false)
      , showPfExclusionBoxes(true)
      , showPfSeedPoints(true)
      , showRecastDebugInfo(false)
      , doNavpHitTest(false)
      , doNavpExclusionBoxHitTest(false)
      , doNavpPfSeedPointHitTest(false)
      , navpScroll(0)
      , loading(false)
      , bBoxPosX(0.0)
      , bBoxPosY(0.0)
      , bBoxPosZ(0.0)
      , bBoxScaleX(1000.0)
      , bBoxScaleY(300.0)
      , bBoxScaleZ(1000.0)
      , lastBBoxPosX(0.0)
      , lastBBoxPosY(0.0)
      , lastBBoxPosZ(0.0)
      , lastBBoxScaleX(1000.0)
      , lastBBoxScaleY(300.0)
      , lastBBoxScaleZ(1000.0)
      , stairsCheckboxValue(false)
      , pruningMode(1.0f)
      , loadNavpName("Load Navp")
      , lastLoadNavpFile(loadNavpName)
      , saveNavpName("Save Navp")
      , lastSaveNavpFile(saveNavpName)
      , building(false) {
    bBoxPos[0] = 0;
    bBoxPos[1] = 0;
    bBoxPos[2] = 0;
    bBoxScale[0] = 1000;
    bBoxScale[1] = 300;
    bBoxScale[2] = 1000;
    Logger::log(NK_INFO,
                ("Setting bbox to (" + std::to_string(bBoxPos[0]) + ", " + std::to_string(bBoxPos[1]) + ", " +
                 std::to_string(bBoxPos[2]) + ") (" + std::to_string(bBoxScale[0]) + ", " + std::to_string(bBoxScale[1])
                 +
                 ", " + std::to_string(bBoxScale[2]) + ")").c_str());
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
    if (showPfSeedPoints) {
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
                {62.0f / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                true,
                {62.0f / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                1.0);
            i++;
        }
    }
}

void Navp::resetDefaults() {
    bBoxPosX = 0.0;
    bBoxPosY = 0.0;
    bBoxPosZ = 0.0;
    bBoxScaleX = 1000.0;
    bBoxScaleY = 300.0;
    bBoxScaleZ = 1000.0;
    lastBBoxPosX = 0.0;
    lastBBoxPosY = 0.0;
    lastBBoxPosZ = 0.0;
    lastBBoxScaleX = 1000.0;
    lastBBoxScaleY = 300.0;
    lastBBoxScaleZ = 1000.0;
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
    if (showPfExclusionBoxes) {
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
                {63.0f / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                true,
                {63.0f / 255.05f, static_cast<float>(highByte) / 255.0f, static_cast<float>(lowByte) / 255.05f},
                1.0);
            i++;
        }
    }
}

char *Navp::openLoadNavpFileDialog(const char *lastNavpFolder) {
    nfdu8filteritem_t filters[2] = {{"Navp files", "navp"}, {"Navp.json files", "navp.json"}};
    return FileUtil::openNfdLoadDialog(filters, 2);
}

char *Navp::openSaveNavpFileDialog(char *lastNavpFolder) {
    nfdu8filteritem_t filters[2] = {{"Navp files", "navp"}, {"Navp.json files", "navp.json"}};
    return FileUtil::openNfdSaveDialog(filters, 2, "output");
}

void Navp::setBBox(const float *pos, const float *scale) {
    Navp &navp = getInstance();
    bBoxPos[0] = pos[0];
    navp.bBoxPosX = pos[0];
    bBoxPos[1] = pos[1];
    navp.bBoxPosY = pos[1];
    bBoxPos[2] = pos[2];
    navp.bBoxPosZ = pos[2];
    bBoxScale[0] = scale[0];
    navp.bBoxScaleX = scale[0];
    bBoxScale[1] = scale[1];
    navp.bBoxScaleY = scale[1];
    bBoxScale[2] = scale[2];
    navp.bBoxScaleZ = scale[2];
    const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    const float bBoxMin[3] = {
        bBoxPos[0] - bBoxScale[0] / 2,
        bBoxPos[1] - bBoxScale[1] / 2,
        bBoxPos[2] - bBoxScale[2] / 2
    };
    const float bBoxMax[3] = {
        bBoxPos[0] + bBoxScale[0] / 2,
        bBoxPos[1] + bBoxScale[1] / 2,
        bBoxPos[2] + bBoxScale[2] / 2
    };
    recastAdapter.setMeshBBox(bBoxMin, bBoxMax);
    Logger::log(NK_INFO,
                ("Setting bbox to (" + std::to_string(pos[0]) + ", " + std::to_string(pos[1]) + ", " +
                 std::to_string(pos[2]) + ") (" + std::to_string(scale[0]) + ", " + std::to_string(scale[1]) + ", " +
                 std::to_string(scale[2]) + ")").c_str());
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
        for (auto vertex: selectedArea.m_edges) {
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
        Vec3 normal = selectedArea.CalculateNormal();
        Vec3 horizontalProjection = {normal.Y, 0.0f, normal.Z};
        float horizontalMagnitude = std::sqrt(
            horizontalProjection.X * horizontalProjection.X + horizontalProjection.Z * horizontalProjection.Z);
        float angleDegrees = 0;
        if (horizontalMagnitude == 0.0f) {
            angleDegrees = 90;
        } else {
            float dotProduct = normal.X * horizontalProjection.X + normal.Z * horizontalProjection.Z;
            float normalMagnitude = std::sqrt(normal.X * normal.X + normal.Y * normal.Y + normal.Z * normal.Z);
            float cosineAngle = dotProduct / (normalMagnitude * horizontalMagnitude);
            cosineAngle = std::clamp(cosineAngle, -1.0f, 1.0f);
            float angleRadians = std::acos(cosineAngle);
            angleDegrees = angleRadians * 180.0f / M_PI;
        }
        msg += " Angle: " + std::to_string(angleDegrees);
        Logger::log(NK_INFO, (msg).c_str());
    }
}

void Navp::setSelectedPfSeedPointIndex(int index) {
    Scene &scene = Scene::getInstance();

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
            renderArea(area, areaIndex == selectedNavpAreaIndex, areaIndex);
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

void Navp::loadNavMeshFileData(const std::string &fileName) {
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

    getInstance().navMeshFileData = s_FileData;
}

void Navp::loadNavMesh(const std::string &fileName, bool isFromJson, bool isFromBuilding, bool loadAirgNavp) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Navp from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.c_str());
    auto start = std::chrono::high_resolution_clock::now();
    Navp &navp = loadAirgNavp ? getAirgInstance() : getInstance();
    navp.loading = true;
    CPPTRACE_TRY
        {
            NavPower::NavMesh newNavMesh = isFromJson
                                               ? LoadNavMeshFromJson(fileName.c_str())
                                               : LoadNavMeshFromBinary(fileName.c_str());
            std::swap(*navp.navMesh, newNavMesh);
            SceneExtract &sceneExtract = SceneExtract::getInstance();
            if (isFromBuilding) {
                navp.setStairsFlags();
                navp.outputNavpFilename = sceneExtract.lastOutputFolder + "\\output.navp";
                OutputNavMesh_JSON_Write(navp.navMesh, (navp.outputNavpFilename + ".json").c_str());
                NavPower::NavMesh reloadedNavMesh = LoadNavMeshFromJson((navp.outputNavpFilename + ".json").c_str());
                std::swap(*navp.navMesh, reloadedNavMesh);
            }
            if (isFromJson) {
                OutputNavMesh_NAVP_Write(navp.navMesh, navp.outputNavpFilename.c_str());
                loadNavMeshFileData(navp.outputNavpFilename);
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
    navp.setSelectedNavpAreaIndex(-1);
    navp.loading = false;
    navp.navpLoaded = true;
    navp.buildAreaMaps();
    InputHandler::setMenuItemEnabled(IDM_FILE_SAVE_NAVP, true);
    InputHandler::setMenuItemEnabled(IDM_BUILD_AIRG, true);
}

void Navp::buildNavp() {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Building Navp at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();
    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    Navp &navp = getInstance();
    navp.updateExclusionBoxConvexVolumes();
    navp.building = true;
    Logger::log(NK_INFO, "Beginning Recast build...");
    if (recastAdapter.handleBuild()) {
        Logger::log(NK_INFO, "Done with Recast build.");
        if (navp.pruningMode != 0.0) {
            Logger::log(NK_INFO, "Pruning areas unreachable by PF Seed Points.");
            recastAdapter.findPfSeedPointAreas();
            recastAdapter.excludeNonReachableAreas();
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        navp.navpBuildDone.push_back(true);
        msg = "Finished building Navp in ";
        msg += std::to_string(duration.count());
        msg += " seconds";
        Logger::log(NK_INFO, msg.data());
        navp.setSelectedNavpAreaIndex(-1);
    } else {
        Logger::log(NK_ERROR, "Error building Navp");
        navp.building = false;
        navp.navpBuildDone.clear();
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

void Navp::handleOpenNavpPressed() {
    char *fileName = openLoadNavpFileDialog(lastLoadNavpFile.data());
    if (fileName) {
        setLastLoadFileName(fileName);
        std::string extension = loadNavpName.substr(loadNavpName.length() - 4, loadNavpName.length());
        std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

        if (extension == "JSON") {
            std::string msg = "Loading Navp.json file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            std::thread loadNavMeshThread(&Navp::loadNavMesh, lastLoadNavpFile.data(), true, false, false);
            loadNavMeshThread.detach();
        } else if (extension == "NAVP") {
            std::string msg = "Loading Navp file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            std::thread loadNavMeshThread(&Navp::loadNavMesh, lastLoadNavpFile.data(), false, false, false);
            loadNavMeshThread.detach();
        }
    }
}

void Navp::handleSaveNavpPressed() {
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
        SceneExtract &sceneExtract = SceneExtract::getInstance();
        if (extension == "JSON") {
            OutputNavMesh_JSON_Write(navMesh, lastSaveNavpFile.data());
        } else if (extension == "NAVP") {
            outputNavpFilename = sceneExtract.lastOutputFolder + "\\output.navp";
            std::string tempOutputJSONFilename = outputNavpFilename + ".json";
            OutputNavMesh_JSON_Write(navMesh, tempOutputJSONFilename.data());
            NavPower::NavMesh newNavMesh = LoadNavMeshFromJson(tempOutputJSONFilename.data());
            std::swap(*navMesh, newNavMesh);
            OutputNavMesh_NAVP_Write(navMesh, lastSaveNavpFile.data());
            buildAreaMaps();
        }
        Logger::log(NK_INFO, "Done saving Navp.");
    }
}

void Navp::drawMenu() {
    Renderer &renderer = Renderer::getInstance();
    Gui &gui = Gui::getInstance();
    int navpMenuHeight = std::min(1190, renderer.height - 20);
    if (imguiBeginScrollArea("Navp menu", 10, renderer.height - navpMenuHeight - 10, 250, navpMenuHeight,
                             &navpScroll)) {
        gui.mouseOverMenu = true;
    }
    imguiLabel("Load Navp from file");
    if (imguiButton(loadNavpName.c_str())) {
        handleOpenNavpPressed();
    }
    imguiLabel("Save Navp to file");
    if (imguiButton(saveNavpName.c_str(), navpLoaded)) {
        handleSaveNavpPressed();
    }
    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    Obj &obj = Obj::getInstance();
    if (imguiButton("Build Navp from Obj and Scene",
                    navpBuildDone.empty() && !building && recastAdapter.inputGeom && obj.objLoaded && !
                    Airg::getInstance().airgBuilding)) {
        std::thread buildNavpThread(&Navp::buildNavp);
        buildNavpThread.detach();
    }
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

    imguiLabel("PFSeedPoint Pruning Mode");
    imguiSlider("Off             Delete             Debug", &pruningMode, 0.0f, 2.0f, 1.0f);

    recastAdapter.handleCommonSettings();

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
    if (imguiSlider("Bounding Box Scale X", &bBoxScaleX, 1.0f, 800.0f, 1.0f)) {
        if (lastBBoxScaleX != bBoxScaleX) {
            bboxChanged = true;
            lastBBoxScaleX = bBoxScaleX;
        }
    }
    if (imguiSlider("Bounding Box Scale Y", &bBoxScaleY, 1.0f, 800.0f, 1.0f)) {
        if (lastBBoxScaleY != bBoxScaleY) {
            bboxChanged = true;
            lastBBoxScaleY = bBoxScaleY;
        }
    }
    if (imguiSlider("Bounding Box Scale Z", &bBoxScaleZ, 1.0f, 800.0f, 1.0f)) {
        if (lastBBoxScaleZ != bBoxScaleZ) {
            bboxChanged = true;
            lastBBoxScaleZ = bBoxScaleZ;
        }
    }
    if (imguiButton("Reset Defaults")) {
        resetDefaults();
        recastAdapter.resetCommonSettings();
        float bBoxPos[3] = {bBoxPosX, bBoxPosY, bBoxPosZ};
        float bBoxScale[3] = {bBoxScaleX, bBoxScaleY, bBoxScaleZ};
        setBBox(bBoxPos, bBoxScale);
    }

    if (bboxChanged) {
        float bBoxPos[3] = {bBoxPosX, bBoxPosY, bBoxPosZ};
        float bBoxScale[3] = {bBoxScaleX, bBoxScaleY, bBoxScaleZ};
        setBBox(bBoxPos, bBoxScale);
    }
    imguiEndScrollArea();
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
    if (!navpBuildDone.empty()) {
        navpLoaded = true;
        RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        SceneExtract &sceneExtract = SceneExtract::getInstance();
        outputNavpFilename = sceneExtract.lastOutputFolder + "\\output.navp.json";
        recastAdapter.save(outputNavpFilename);
        std::thread loadNavMeshThread(&Navp::loadNavMesh, outputNavpFilename.data(), true, true, false);
        loadNavMeshThread.detach();
        navpBuildDone.clear();
        building = false;
    }
}
