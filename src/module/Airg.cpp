#include "../../include/NavKit/module/Airg.h"

#include <filesystem>
#include <fstream>
#include <numbers>

#include <SDL.h>
#include <GL/glew.h>
#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/model/VisionData.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/GridGenerator.h"
#include "../../include/RecastDemo/imgui.h"
#include "../../include/ResourceLib_HM3/ResourceLib_HM3.h"
#include "../../include/ResourceLib_HM3/ResourceConverter.h"
#include "../../include/ResourceLib_HM3/ResourceGenerator.h"
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"

Airg::Airg()
    : airgName("Load Airg")
      , lastLoadAirgFile(airgName)
      , saveAirgName("Save Airg")
      , lastSaveAirgFile(saveAirgName)
      , airgLoaded(false)
      , airgLoading(false)
      , airgBuilding(false)
      , connectWaypointModeEnabled(false)
      , showAirg(true)
      , showAirgIndices(false)
      , showRecastDebugInfo(false)
      , cellColorSource(OFF)
      , airgResourceConverter(HM3_GetConverterForResource("AIRG"))
      , airgResourceGenerator(HM3_GetGeneratorForResource("AIRG"))
      , reasoningGrid(new ReasoningGrid())
      , airgScroll(0)
      , selectedWaypointIndex(-1)
      , doAirgHitTest(false)
      , buildingVisionAndDeadEndData(false) {
}

const int Airg::AIRG_MENU_HEIGHT = 310;

Airg::~Airg() = default;

void Airg::resetDefaults() {
    Grid &grid = Grid::getInstance();
    grid.spacing = 2.25;
    grid.xOffset = 0;
    grid.yOffset = 0;
}

void Airg::setLastLoadFileName(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        airgName = fileName;
        lastLoadAirgFile = airgName;
        airgLoaded = false;
        airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
    }
}

void Airg::setLastSaveFileName(const char *fileName) {
    saveAirgName = fileName;
    lastSaveAirgFile = saveAirgName;
    saveAirgName = saveAirgName.substr(saveAirgName.find_last_of("/\\") + 1);
}

void Airg::drawMenu() {
    Renderer &renderer = Renderer::getInstance();
    Gui &gui = Gui::getInstance();
    Grid &grid = Grid::getInstance();
    int airgMenuHeight = std::min(AIRG_MENU_HEIGHT,
                                  renderer.height - 10 - Settings::SETTINGS_MENU_HEIGHT - Scene::SCENE_MENU_HEIGHT -
                                  SceneExtract::SCENE_EXTRACT_MENU_HEIGHT - Obj::OBJ_MENU_HEIGHT - 20 - 10);

    if (imguiBeginScrollArea("Airg menu", renderer.width - 250 - 10,
                             renderer.height - 10 - Settings::SETTINGS_MENU_HEIGHT - Scene::SCENE_MENU_HEIGHT -
                             SceneExtract::SCENE_EXTRACT_MENU_HEIGHT - Obj::OBJ_MENU_HEIGHT - airgMenuHeight - 20, 250,
                             airgMenuHeight, &airgScroll))
        gui.mouseOverMenu = true;

    imguiLabel("Load Airg from file");
    if (imguiButton(airgName.c_str(), (!airgLoading && airgSaveState.empty()))) {
        char *fileName = openAirgFileDialog(lastLoadAirgFile.data());
        if (fileName) {
            setLastLoadFileName(fileName);
            std::string extension = airgName.substr(airgName.length() - 4, airgName.length());
            std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

            if (extension == "JSON") {
                delete reasoningGrid;
                reasoningGrid = new ReasoningGrid();
                std::string msg = "Loading Airg.Json file: '";
                msg += fileName;
                msg += "'...";
                Logger::log(NK_INFO, msg.data());
                std::thread loadAirgThread(&Airg::loadAirg, this, fileName, true);
                loadAirgThread.detach();
            } else if (extension == "AIRG") {
                delete reasoningGrid;
                reasoningGrid = new ReasoningGrid();
                std::string msg = "Loading Airg file: '";
                msg += fileName;
                msg += "'...";
                Logger::log(NK_INFO, msg.data());
                std::thread loadAirgThread(&Airg::loadAirg, this, fileName, false);
                loadAirgThread.detach();
            }
        }
    }
    imguiLabel("Save Airg to file");
    if (imguiButton(saveAirgName.c_str(), (airgLoaded && airgSaveState.empty()))) {
        char *fileName = openSaveAirgFileDialog(lastLoadAirgFile.data());
        if (fileName) {
            setLastSaveFileName(fileName);
            std::string fileNameString = std::string{fileName};
            std::string extension = fileNameString.substr(fileNameString.length() - 4, fileNameString.length());
            std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
            std::string msg = "Saving Airg";
            if (extension == "JSON") {
                msg += ".json";
            }
            msg += " file: '";
            msg += fileName;
            msg += "'...";
            Logger::log(NK_INFO, msg.data());
            std::thread saveAirgThread(&Airg::saveAirg, this, fileName, extension == "JSON");
            saveAirgThread.detach();
        }
    }
    Navp &navp = Navp::getInstance();

    if (imguiButton("Build Airg from Navp",
                    navp.navpLoaded && !airgLoading && airgSaveState.empty() && !airgBuilding)) {
        airgLoaded = false;
        airgBuilding = true;
        delete reasoningGrid;
        reasoningGrid = new ReasoningGrid();
        std::string msg = "Building Airg from Navp";
        Logger::log(NK_INFO, msg.data());
        build();
    }
    if (imguiButton("Load grid bounds from Navp", navp.navpLoaded && !airgLoading && airgSaveState.empty())) {
        Logger::log(NK_INFO, "Loading Airg grid bounds from Navp");
        grid.loadBoundsFromNavp();
    }
    if (imguiButton("Connect Waypoint", (airgLoaded && selectedWaypointIndex != -1 && !connectWaypointModeEnabled))) {
        connectWaypointModeEnabled = true;
        std::string msg = "Entering Connect Waypoint mode. Start waypoint: " + std::to_string(selectedWaypointIndex);
        Logger::log(NK_INFO, msg.data());
    }
    char selectedWaypointText[64];
    snprintf(selectedWaypointText, 64,
             selectedWaypointIndex != -1 ? "Selected Waypoint Index: %d" : "Selected Waypoint Index: None",
             selectedWaypointIndex);
    imguiValue(selectedWaypointText);
    // imguiLabel("Cell color data source");
    // imguiSlider("Off   Bitmap    Vision Data    Layer", &cellColorSource, 0.0f, 3.0f, 1.0f);
    float lastSpacing = grid.spacing;
    if (imguiSlider("Spacing", &grid.spacing, 0.1f, 4.0f, 0.05f)) {
        if (lastSpacing != grid.spacing) {
            grid.saveSpacing(grid.spacing);
            Logger::log(NK_INFO, ("Setting spacing to: " + std::to_string(grid.spacing)).c_str());
        }
    }
    float lastXOffset = grid.xOffset;
    if (imguiSlider("X Offset", &grid.xOffset, -grid.spacing, grid.spacing, 0.05f)) {
        if (lastXOffset != grid.xOffset) {
            Logger::log(NK_INFO, ("Setting X offset to: " + std::to_string(grid.xOffset)).c_str());
        }
    }
    float lastZOffset = grid.yOffset;
    if (imguiSlider("Z Offset", &grid.yOffset, -grid.spacing, grid.spacing, 0.05f)) {
        if (lastZOffset != grid.yOffset) {
            Logger::log(NK_INFO, ("Setting Z offset to: " + std::to_string(grid.yOffset)).c_str());
        }
    }
    if (imguiButton("Reset Defaults")) {
        Logger::log(NK_INFO, "Resetting Airg Default settings");
        resetDefaults();
    }

    imguiEndScrollArea();
}

void Airg::finalizeSave() {
    if (airgSaveState.size() == 2) {
        airgSaveState.clear();
    }
}

void Airg::connectWaypoints(const int startWaypointIndex, const int endWaypointIndex) const {
    Waypoint &startWaypoint = reasoningGrid->m_WaypointList[startWaypointIndex];
    Waypoint &endWaypoint = reasoningGrid->m_WaypointList[endWaypointIndex];
    Vec3 startPos = {startWaypoint.vPos.x, startWaypoint.vPos.y, startWaypoint.vPos.z};
    Vec3 endPos = {endWaypoint.vPos.x, endWaypoint.vPos.y, endWaypoint.vPos.z};
    Vec3 waypointDirectionVec = endPos - startPos;
    Vec3 directionNormalized = waypointDirectionVec / waypointDirectionVec.GetMagnitude();
    float maxParalellization = -2;
    int bestDirection = -1;
    for (int direction = 0; direction < 8; direction++) {
        float dx = 0, dy = 0;
        if (direction == 1 || direction == 2 || direction == 3) {
            dx = 1;
        } else if (direction == 5 || direction == 6 || direction == 7) {
            dx = -1;
        }
        if (direction == 7 || direction == 0 || direction == 1) {
            dy = -1;
        } else if (direction == 3 || direction == 4 || direction == 5) {
            dy = 1;
        }
        Vec3 directionVec = {dx, dy, 0.0f};
        float dot = directionNormalized.Dot(directionVec);
        if (dot > maxParalellization) {
            maxParalellization = dot;
            bestDirection = direction;
        }
    }
    startWaypoint.nNeighbors[bestDirection] = endWaypointIndex;
    endWaypoint.nNeighbors[(bestDirection + 4) % 8] = startWaypointIndex;
    Logger::log(NK_INFO,
                ("Connected waypoints: " + std::to_string(startWaypointIndex) + " and " + std::to_string(
                     endWaypointIndex)).c_str());
}

char *Airg::openAirgFileDialog(const char *lastAirgFolder) {
    nfdu8filteritem_t filters[2] = {{"Airg files", "airg"}, {"Airg.json files", "airg.json"}};
    return FileUtil::openNfdLoadDialog(filters, 2, lastAirgFolder);
}

char *Airg::openSaveAirgFileDialog(char *lastAirgFolder) {
    nfdu8filteritem_t filters[2] = {{"Airg files", "airg"}, {"Airg.json files", "airg.json"}};
    return FileUtil::openNfdSaveDialog(filters, 2, "output", lastAirgFolder);
}

void renderWaypoint(const Waypoint &waypoint, const bool fan) {
    if (fan) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(waypoint.vPos.x, waypoint.vPos.z + 0.03, -waypoint.vPos.y);
    } else {
        glBegin(GL_LINE_LOOP);
    }

    const float r = 0.1f;
    for (int i = 0; i < 8; i++) {
        const float a = (float) i / 8.0f * std::numbers::pi * 2;
        const float fx = (float) waypoint.vPos.x + cosf(a) * r;
        const float fy = (float) waypoint.vPos.y + sinf(a) * r;
        const float fz = (float) waypoint.vPos.z + 0.03f;
        glVertex3f(fx, fz, -fy);
    }
    if (fan) {
        const float fx = (float) waypoint.vPos.x + cosf(0) * r;
        const float fy = (float) waypoint.vPos.y + sinf(0) * r;
        const float fz = (float) waypoint.vPos.z + 0.03f;
        glVertex3f(fx, fz, -fy);
    }
    glEnd();
}

int visibilityDataSize(ReasoningGrid *reasoningGrid, int waypointIndex) {
    int offset1 = reasoningGrid->m_WaypointList[waypointIndex].nVisionDataOffset;
    int offset2;
    if (waypointIndex < (reasoningGrid->m_WaypointList.size() - 1)) {
        offset2 = reasoningGrid->m_WaypointList[waypointIndex + 1].nVisionDataOffset;
    } else {
        offset2 = reasoningGrid->m_pVisibilityData.size();
    }
    return offset2 - offset1;
}

void Airg::build() {
    std::thread buildAirgThread(&GridGenerator::build);
    buildAirgThread.detach();
}

// Render Layer Index
void Airg::renderLayerIndices(int waypointIndex, bool selected) {
    float size = 25;
    const Waypoint &waypoint = reasoningGrid->m_WaypointList[waypointIndex];
    float minX = reasoningGrid->m_Properties.vMin.x;
    float minY = reasoningGrid->m_Properties.vMin.y;
    int xi = (int) floor((waypoint.vPos.x - minX) / reasoningGrid->m_Properties.fGridSpacing);
    int yi = (int) floor((waypoint.vPos.y - minY) / reasoningGrid->m_Properties.fGridSpacing);

    float x = xi * reasoningGrid->m_Properties.fGridSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 + minX;
    float y = yi * reasoningGrid->m_Properties.fGridSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 + minY;

    glColor4f(0.8, 0.8, 0.8, 0.6);
    glBegin(GL_LINE_LOOP);
    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glEnd();
    glBegin(GL_POLYGON);

    const unsigned int colorRgb = (waypoint.nLayerIndex << 6) | 0x80000000;
    unsigned char a = (colorRgb >> 24) & 0xFF;
    unsigned char b = (colorRgb >> 16) & 0xFF;
    unsigned char g = (colorRgb >> 8) & 0xFF;
    unsigned char r = colorRgb & 0xFF;
    Vec4 color{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    glColor4f(color.x, color.y, color.z, color.w);

    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glEnd();
}

void Airg::renderCellBitmaps(int waypointIndex, bool selected) {
    float size = 25;
    const Waypoint &waypoint = reasoningGrid->m_WaypointList[waypointIndex];
    float minX = reasoningGrid->m_Properties.vMin.x;
    float minY = reasoningGrid->m_Properties.vMin.y;
    float x = waypoint.xi * reasoningGrid->m_Properties.fGridSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 +
              minX;
    float y = waypoint.yi * reasoningGrid->m_Properties.fGridSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 +
              minY;
    std::vector<uint8_t> waypointVisionData;
    for (int i = 0; i < 25; i++) {
        waypointVisionData.push_back(waypoint.cellBitmap[i] * 255);
    }
    glColor4f(0.8, 0.8, 0.8, 0.6);
    glBegin(GL_LINE_LOOP);
    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glEnd();
    float bw = reasoningGrid->m_Properties.fGridSpacing / sqrt(size);
    int numBoxesPerSide = reasoningGrid->m_Properties.fGridSpacing / bw;
    for (int byi = 0; byi < numBoxesPerSide; byi++) {
        for (int bxi = 0; bxi < numBoxesPerSide; bxi++) {
            float bx = x - reasoningGrid->m_Properties.fGridSpacing / 2 + bxi * bw;
            float by = -(y - reasoningGrid->m_Properties.fGridSpacing / 2 + (byi + 1) * bw);
            uint8_t boxVisionData = waypointVisionData[numBoxesPerSide * byi + bxi];
            float color = boxVisionData / 255.0;
            glColor4f(color, color, color, 0.3);
            glBegin(GL_POLYGON);
            float zChange = selected ? 0.001 : 0.01;
            glVertex3f(bx, waypoint.vPos.z + zChange, by);
            glVertex3f(bx, waypoint.vPos.z + zChange, by + bw);
            glVertex3f(bx + bw, waypoint.vPos.z + zChange, by + bw);
            glVertex3f(bx + bw, waypoint.vPos.z + zChange, by);
            glEnd();
        }
    }
}

void Airg::renderVisionData(int waypointIndex, bool selected) {
    float size = visibilityDataSize(reasoningGrid, waypointIndex);
    const Waypoint &waypoint = reasoningGrid->m_WaypointList[waypointIndex];
    float minX = reasoningGrid->m_Properties.vMin.x;
    float minY = reasoningGrid->m_Properties.vMin.y;
    float x = waypoint.vPos.x;
    float y = waypoint.vPos.y;
    std::vector<uint8_t> waypointVisionData = reasoningGrid->getWaypointVisionData(waypointIndex);
    glColor4f(0.8, 0.8, 0.8, 0.6);
    glBegin(GL_LINE_LOOP);
    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x - reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
    glVertex3f(x + reasoningGrid->m_Properties.fGridSpacing / 2, waypoint.vPos.z + 0.01,
               -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
    glEnd();
    float bw = reasoningGrid->m_Properties.fGridSpacing / sqrt(size);
    int numBoxesPerSide = reasoningGrid->m_Properties.fGridSpacing / bw;
    for (int byi = 0; byi < numBoxesPerSide; byi++) {
        for (int bxi = 0; bxi < numBoxesPerSide; bxi++) {
            float bx = x - reasoningGrid->m_Properties.fGridSpacing / 2 + bxi * bw;
            float by = -y - reasoningGrid->m_Properties.fGridSpacing / 2 + byi * bw;
            uint8_t boxVisionData = waypointVisionData[numBoxesPerSide * byi + bxi];
            float color = boxVisionData / 255.0;
            glColor4f(0.0, color, 0.0, 0.3);
            glBegin(GL_POLYGON);
            float zChange = selected ? 0.001 : 0.01;
            glVertex3f(bx, waypoint.vPos.z + zChange, by);
            glVertex3f(bx, waypoint.vPos.z + zChange, by + bw);
            glVertex3f(bx + bw, waypoint.vPos.z + zChange, by + bw);
            glVertex3f(bx + bw, waypoint.vPos.z + zChange, by);
            glEnd();
        }
    }
}

void Airg::renderAirg() {
    if (selectedWaypointIndex >= reasoningGrid->m_WaypointList.size()) {
        selectedWaypointIndex = -1;
    }
    int numWaypoints = reasoningGrid->m_WaypointList.size();
    for (size_t i = 0; i < numWaypoints; i++) {
        const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
        int size = visibilityDataSize(reasoningGrid, i);
        // cellColorSource options: 0 Off, 1 Bitmap, 2 Vision Data, 3 Layer
        if (cellColorSource == CellColorDataSource::AIRG_BITMAP) {
            renderCellBitmaps(i, i == selectedWaypointIndex);
        }
        if (cellColorSource == LAYER) {
            renderVisionData(i, i == selectedWaypointIndex);
        }
        if (cellColorSource == VISION_DATA) {
            renderLayerIndices(i, i == selectedWaypointIndex);
        }

        Vec4 color{0, 0, 1, 0.5};

        glColor4f(color.x, color.y, color.z, color.w);
        renderWaypoint(waypoint, selectedWaypointIndex == i);

        for (int neighborIndex = 0; neighborIndex < waypoint.nNeighbors.size(); neighborIndex++) {
            if (waypoint.nNeighbors[neighborIndex] != 65535 && waypoint.nNeighbors[neighborIndex] < reasoningGrid->
                m_WaypointList.size()) {
                const Waypoint &neighbor = reasoningGrid->m_WaypointList[waypoint.nNeighbors[neighborIndex]];
                glBegin(GL_LINES);
                glVertex3f(waypoint.vPos.x, waypoint.vPos.z + 0.01f, -waypoint.vPos.y);
                glVertex3f(neighbor.vPos.x, neighbor.vPos.z + 0.01f, -neighbor.vPos.y);
                glEnd();
            }
        }
    }
    Renderer &renderer = Renderer::getInstance();
    if (showAirgIndices) {
        for (size_t i = 0; i < numWaypoints; i++) {
            const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
            renderer.drawText(std::to_string(i), {waypoint.vPos.x, waypoint.vPos.z + 0.1f, -waypoint.vPos.y},
                              {1, .7f, .7f});
        }
    }
}

void Airg::renderAirgForHitTest() {
    if (showAirg) {
        int numWaypoints = reasoningGrid->m_WaypointList.size();
        for (size_t i = 0; i < numWaypoints; i++) {
            const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
            glColor3ub(61, i / 255, i % 255);
            renderWaypoint(waypoint, true);
        }
    }
}

void Airg::setSelectedAirgWaypointIndex(int index) {
    if (index == -1 && selectedWaypointIndex != -1) {
        Logger::log(NK_INFO, ("Deselected waypoint: " + std::to_string(selectedWaypointIndex)).c_str());
    }
    selectedWaypointIndex = index;
    if (index != -1 && index < reasoningGrid->m_WaypointList.size()) {
        Waypoint waypoint = reasoningGrid->m_WaypointList[index];
        std::string msg = "Airg Waypoint " + std::to_string(index);
        for (int i = 0; i < waypoint.nNeighbors.size(); i++) {
            int neighborIndex = waypoint.nNeighbors[i];
            if (neighborIndex != 65535) {
                msg += ":  Neighbor " + std::to_string(i) + ": " + std::to_string(neighborIndex);
            }
        }
        Logger::log(NK_INFO, msg.c_str());
        Logger::log(NK_INFO,
                    ("Waypoint position: X: " + std::to_string(waypoint.vPos.x) + " Y: " +
                     std::to_string(waypoint.vPos.y) + " Z: " + std::to_string(waypoint.vPos.z) + "   XI: " +
                     std::to_string(waypoint.xi) + " YI: " + std::to_string(waypoint.yi) + " ZI: " + std::to_string(
                         waypoint.zi)).c_str());
        msg = "  Vision Data Offset: " + std::to_string(waypoint.nVisionDataOffset);
        msg += "  Layer Index: " + std::to_string(waypoint.nLayerIndex);
        int nextWaypointOffset = (index + 1) < reasoningGrid->m_WaypointList.size()
                                     ? reasoningGrid->m_WaypointList[index + 1].nVisionDataOffset
                                     : reasoningGrid->m_pVisibilityData.size();
        int visibilityDataSize = nextWaypointOffset - waypoint.nVisionDataOffset;
        msg += "  Visibility Data size: " + std::to_string(visibilityDataSize);

        std::vector<uint8_t> waypointVisibilityData = reasoningGrid->getWaypointVisionData(index);

        if (!waypointVisibilityData.empty()) {
            std::string waypointVisibilityDataString;
            char numHex[3];
            msg += "  Visibility Data Type: " + std::to_string(waypointVisibilityData[0]); // +" Visibility data:";
            Logger::log(NK_INFO, msg.c_str());
            for (int count = 2; count < waypointVisibilityData.size(); count++) {
                uint8_t num = waypointVisibilityData[count];
                sprintf(numHex, "%02X", num);
                if (numHex[0] == '0' && numHex[1] == '0') {
                    numHex[0] = '~';
                    numHex[1] = '~';
                }
                waypointVisibilityDataString += std::string{numHex};
                waypointVisibilityDataString += " ";
                if ((count - 1) % 8 == 0) {
                    waypointVisibilityDataString += " ";
                }
                if ((count - 1) % 96 == 0) {
                    //Logger::log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
                    waypointVisibilityDataString = "";
                }
            }
        }
        //Logger::log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
        const unsigned int colorRgb = (waypoint.nLayerIndex << 6) | 0xC0000000;
        std::string hexColor = std::format("{:x}", colorRgb);
        Logger::log(NK_INFO, ("Layer Index RGB " + hexColor).c_str());
    }
}

void Airg::saveAirg(Airg *airg, std::string fileName, bool isJson) {
    airg->airgSaveState.push_back(true);
    std::string s_OutputFileName = std::filesystem::path(fileName).string();
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Airg to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();

    std::string tempJsonFile = fileName;
    tempJsonFile += ".temp.json";
    if (!isJson) {
        s_OutputFileName = std::filesystem::path(tempJsonFile).string();
    }
    std::filesystem::remove(s_OutputFileName);
    // Write the airg to JSON file
    std::ofstream fileOutputStream(s_OutputFileName);
    airg->reasoningGrid->writeJson(fileOutputStream);
    fileOutputStream.close();

    if (!isJson) {
        airg->airgResourceGenerator->FromJsonFileToResourceFile(tempJsonFile.data(), std::string{fileName}.c_str(),
                                                                false);
        std::filesystem::remove(tempJsonFile);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished saving Airg to " + std::string{fileName} + " in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.data());
    airg->airgSaveState.push_back(true);
}

void Airg::loadAirg(Airg *airg, char *fileName, bool isFromJson) {
    airg->airgLoading = true;
    airg->airgLoaded = false;
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Airg from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();

    std::string jsonFileName = fileName;
    if (!isFromJson) {
        std::string nameWithoutExtension = jsonFileName.substr(0, jsonFileName.length() - 5);

        jsonFileName = nameWithoutExtension + ".temp.airg.json";
        airg->airgResourceConverter->FromResourceFileToJsonFile(fileName, jsonFileName.data());
    }
    airg->reasoningGrid->readJson(jsonFileName.data());
    Grid::getInstance().saveSpacing(airg->reasoningGrid->m_Properties.fGridSpacing);
    if (!isFromJson) {
        std::filesystem::remove(jsonFileName);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished loading Airg in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.data());
    Logger::log(NK_INFO,
                ("Waypoint count: " + std::to_string(airg->reasoningGrid->m_WaypointList.size()) +
                 ", Visibility Data size: " + std::to_string(airg->reasoningGrid->m_pVisibilityData.size()) +
                 ", Visibility Data points per waypoint: " + std::to_string(
                     static_cast<double>(airg->reasoningGrid->m_pVisibilityData.size()) / static_cast<double>(airg
                         ->reasoningGrid->m_WaypointList.size()))).c_str());
    int lastVisionDataSize = 0;
    int count = 1;
    int total = 0;
    std::map<int, int> visionDataOffsetCounts;
    for (int i = 1; i < airg->reasoningGrid->m_WaypointList.size(); i++) {
        Waypoint w1 = airg->reasoningGrid->m_WaypointList[i - 1];
        Waypoint w2 = airg->reasoningGrid->m_WaypointList[i];
        int visionDataSize = w2.nVisionDataOffset - w1.nVisionDataOffset;
        total += visionDataSize;
        if (lastVisionDataSize != visionDataSize) {
            lastVisionDataSize = visionDataSize;
            Logger::log(NK_INFO,
                        ("Vision Data Offset[" + std::to_string(i - 1) + "]: " +
                         std::to_string(w1.nVisionDataOffset) + " Vision Data Offset[" + std::to_string(i) + "]: "
                         + std::to_string(w2.nVisionDataOffset) + " Difference : " +
                         std::to_string(lastVisionDataSize) + " Count: " + std::to_string(count)).c_str());
            if (visionDataOffsetCounts.count(visionDataSize) > 0) {
                int cur = visionDataOffsetCounts[visionDataSize];
                visionDataOffsetCounts[visionDataSize] = cur + 1;
            } else {
                visionDataOffsetCounts[visionDataSize] = 1;
            }
            count = 1;
        } else {
            count++;
        }
    }
    int finalVisionDataSize = airg->reasoningGrid->m_pVisibilityData.size() - airg->reasoningGrid->m_WaypointList[
                                  airg->reasoningGrid->m_WaypointList.size() - 1].nVisionDataOffset;
    Logger::log(NK_INFO,
                ("Vision Data Offset[" + std::to_string(airg->reasoningGrid->m_WaypointList.size() - 1) + "]: " +
                 std::to_string(
                     airg->reasoningGrid->m_WaypointList[airg->reasoningGrid->m_WaypointList.size() - 1].
                     nVisionDataOffset) + " Max Visibility: " + std::to_string(
                     airg->reasoningGrid->m_pVisibilityData.size()) + " Difference : " + std::to_string(
                     finalVisionDataSize)).c_str());
    total += finalVisionDataSize;
    Logger::log(NK_INFO,
                ("Total: " + std::to_string(total) + " Max Visibility: " + std::to_string(
                     airg->reasoningGrid->m_pVisibilityData.size())).c_str());
    Logger::log(NK_INFO, "Visibility data offset map:");
    for (auto &pair: visionDataOffsetCounts) {
        VisionData visionData = VisionData::GetVisionDataType(pair.first);
        Logger::log(NK_INFO,
                    ("Offset difference: " + std::to_string(pair.first) + " Color: " + visionData.getName() +
                     " Count: " + std::to_string(pair.second)).c_str());
    }

    airg->airgLoading = false;
    airg->airgLoaded = true;
    Grid::getInstance().loadBoundsFromAirg();
}
