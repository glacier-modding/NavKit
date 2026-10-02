#include "../../include/NavKit/module/Airg.h"

#include <filesystem>
#include <numbers>
#include <stdexcept>

#include <iomanip>
#include <SDL.h>
#include <sstream>
#include <string>
#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/model/VisionData.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/RecastDemo/imguiRenderGL.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/GridGenerator.h"

Airg::Airg() :
    airgName("Load Airg"), lastLoadAirgFile(airgName), saveAirgName("Save Airg"), lastSaveAirgFile(saveAirgName),
    airgLoaded(false), airgLoading(false), airgBuilding(false), connectWaypointModeEnabled(false), showAirg(true),
    showAirgIndices(false), showRecastDebugInfo(false), cellColorSource(OFF), reasoningGrid(new ReasoningGrid()),
    selectedWaypointIndex(-1), doAirgHitTest(false), buildingVisionAndDeadEndData(false) {}

Airg::~Airg() = default;

wxDialog* Airg::hAirgDialog = nullptr;
wxDialog* Airg::hExtractAirgDialog = nullptr;
std::string Airg::selectedRpkgAirg{};
std::map<std::string, std::string> Airg::airgHashIoiStringMap;
std::mutex Airg::airgHashIoiStringMapMutex;

GLuint Airg::airgTriVao = 0;
GLuint Airg::airgTriVbo = 0;
GLuint Airg::airgLineVao = 0;
GLuint Airg::airgLineVbo = 0;
int Airg::airgTriCount = 0;
int Airg::airgLineCount = 0;
bool Airg::airgDirty = true;

GLuint Airg::airgHitTestVao = 0;
GLuint Airg::airgHitTestVbo = 0;
int Airg::airgHitTestCount = 0;

static std::string formatFloat(const float val) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    return ss.str();
}

void Airg::showAirgDialog() {
    if (hAirgDialog) {
        hAirgDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Airg Properties", wxDefaultPosition, wxSize(360, 200));
    hAirgDialog = dialog;
    auto* gridSizer = new wxFlexGridSizer(3, 3, 8, 8);
    gridSizer->Add(new wxStaticText(dialog, wxID_ANY, "Spacing:"), 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->Add(new wxSlider(dialog, IDC_SLIDER_SPACING, 0, 0, 78), 1, wxEXPAND);
    gridSizer->Add(new wxStaticText(dialog, IDC_STATIC_SPACING_VAL, ""), 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->Add(new wxStaticText(dialog, wxID_ANY, "X Offset:"), 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->Add(new wxSlider(dialog, IDC_SLIDER_XOFFSET, 0, 0, 1), 1, wxEXPAND);
    gridSizer->Add(new wxStaticText(dialog, IDC_STATIC_XOFFSET_VAL, ""), 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->Add(new wxStaticText(dialog, wxID_ANY, "Z Offset:"), 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->Add(new wxSlider(dialog, IDC_SLIDER_ZOFFSET, 0, 0, 1), 1, wxEXPAND);
    gridSizer->Add(new wxStaticText(dialog, IDC_STATIC_ZOFFSET_VAL, ""), 0, wxALIGN_CENTER_VERTICAL);
    gridSizer->AddGrowableCol(1, 1);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* reset = new wxButton(dialog, IDC_BUTTON_RESET_DEFAULTS, "Reset Defaults");
    buttons->Add(reset);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(gridSizer, 1, wxEXPAND | wxALL, 12);
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    dialog->SetSizer(sizer);
    UpdateDialogControls(dialog);
    const auto sliderChanged = [dialog](wxCommandEvent& event) {
        Grid& grid = Grid::getInstance();
        if (event.GetId() == IDC_SLIDER_SPACING) {
            const float oldSpacing = grid.spacing;
            grid.spacing = 0.1f + static_cast<float>(event.GetInt()) * 0.05f;
            if (oldSpacing != grid.spacing) {
                grid.saveSpacing(grid.spacing);
                Airg::UpdateDialogControls(dialog);
                airgDirty = true;
            }
        } else if (event.GetId() == IDC_SLIDER_XOFFSET) {
            grid.xOffset = -grid.spacing + static_cast<float>(event.GetInt()) * 0.05f;
            dialog->FindWindow(IDC_STATIC_XOFFSET_VAL)->SetLabel(formatFloat(grid.xOffset));
            airgDirty = true;
        } else if (event.GetId() == IDC_SLIDER_ZOFFSET) {
            grid.yOffset = -grid.spacing + static_cast<float>(event.GetInt()) * 0.05f;
            dialog->FindWindow(IDC_STATIC_ZOFFSET_VAL)->SetLabel(formatFloat(grid.yOffset));
            airgDirty = true;
        }
    };
    for (const int id : {IDC_SLIDER_SPACING, IDC_SLIDER_XOFFSET, IDC_SLIDER_ZOFFSET}) {
        dialog->Bind(wxEVT_SLIDER, sliderChanged, id);
    }
    reset->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) {
        Logger::log(NK_INFO, "Resetting Airg Default settings");
        Airg::resetDefaults();
        Airg::UpdateDialogControls(dialog);
        airgDirty = true;
    });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hAirgDialog == dialog) {
            hAirgDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->SetSizeHints(320, 150);
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
}

void Airg::UpdateDialogControls(wxDialog* dialog) {
    const Grid& grid = Grid::getInstance();
    auto* hSliderSpacing = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_SPACING));
    auto* hSliderX = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_XOFFSET));
    auto* hSliderZ = static_cast<wxSlider*>(dialog->FindWindow(IDC_SLIDER_ZOFFSET));
    hSliderSpacing->SetRange(0, 78);
    const int spacingPos = static_cast<int>((grid.spacing - 0.1f) / 0.05f);
    hSliderSpacing->SetValue(spacingPos);
    dialog->FindWindow(IDC_STATIC_SPACING_VAL)->SetLabel(formatFloat(grid.spacing));
    const int range = static_cast<int>((grid.spacing * 2) / 0.05f);
    hSliderX->SetRange(0, range);
    const int xPos = static_cast<int>((grid.xOffset + grid.spacing) / 0.05f);
    hSliderX->SetValue(xPos);
    dialog->FindWindow(IDC_STATIC_XOFFSET_VAL)->SetLabel(formatFloat(grid.xOffset));
    hSliderZ->SetRange(0, range);
    const int zPos = static_cast<int>((grid.yOffset + grid.spacing) / 0.05f);
    hSliderZ->SetValue(zPos);
    dialog->FindWindow(IDC_STATIC_ZOFFSET_VAL)->SetLabel(formatFloat(grid.yOffset));
}

void Airg::resetDefaults() {
    Grid& grid = Grid::getInstance();
    grid.spacing = 2.25;
    grid.xOffset = 0;
    grid.yOffset = 0;
}

void Airg::setLastLoadFileName(const char* fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        airgName = fileName;
        lastLoadAirgFile = airgName;
        airgLoaded = false;
        Menu::updateMenuState();
        airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
    }
}

void Airg::setLastSaveFileName(const char* fileName) {
    saveAirgName = fileName;
    lastSaveAirgFile = saveAirgName;
    saveAirgName = saveAirgName.substr(saveAirgName.find_last_of("/\\") + 1);
}

void Airg::loadAirgFromFile(const std::string& fileName) {
    setLastLoadFileName(fileName.c_str());
    std::string fileNameStr = fileName;
    std::string extension = airgName.substr(airgName.length() - 4, airgName.length());
    std::ranges::transform(extension, extension.begin(), ::toupper);

    if (extension == "AIRG") {
        delete reasoningGrid;
        reasoningGrid = new ReasoningGrid();
        Logger::log(NK_INFO, "Loading Airg file: '%s'...", fileNameStr.c_str());
        backgroundWorker.emplace(&Airg::loadAirg, this, fileNameStr);
    }
    airgDirty = true;
}

void Airg::handleOpenAirgClicked() {
    if (const char* fileName = openAirgFileDialog()) {
        loadedAirgText = fileName;
        loadAirgFromFile(fileName);
    }
}

void Airg::handleSaveAirgClicked() {
    if (char* fileName = openSaveAirgFileDialog()) {
        setLastSaveFileName(fileName);
        const auto fileNameString = std::string{fileName};
        std::string msg = "Saving Airg file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        backgroundWorker.emplace(&Airg::saveAirg, this, fileNameString);
    }
}

bool Airg::canLoad() const {
    return !airgLoading && airgSaveState.empty();
}

bool Airg::canSave() const {
    return airgLoaded && airgSaveState.empty();
}

bool Airg::canBuildAirg() const {
    const Navp& navp = Navp::getInstance();
    return navp.navpLoaded && !airgLoading && airgSaveState.empty() && !airgBuilding;
}

void Airg::handleBuildAirgClicked() {
    if (const Navp& navp = Navp::getInstance(); Navp::getTotalAreaCount(navp.navMesh) > 65535) {
        Logger::log(NK_ERROR,
            "Loaded NAVP has too many areas. Ensure the scene has PF Seed Points and has a fully contained boundary.");
    }
    airgLoaded = false;
    Menu::updateMenuState();
    airgBuilding = true;
    delete reasoningGrid;
    reasoningGrid = new ReasoningGrid();
    airgDirty = true;
    airgDirty = true;
    Logger::log(NK_INFO, "Building Airg from Airg");
    backgroundWorker.emplace(&Airg::buildAirg, this);
}

bool Airg::canEnterConnectWaypointMode() const {
    return airgLoaded && selectedWaypointIndex != -1;
}

bool Airg::canEnterDisconnectWaypointMode() const {
    return airgLoaded && selectedWaypointIndex != -1;
}

void Airg::handleConnectWaypointClicked() {
    connectWaypointModeEnabled = !connectWaypointModeEnabled;
    if (connectWaypointModeEnabled) {
        Logger::log(NK_INFO, "Entering Connect Waypoint mode. Start waypoint: %d", selectedWaypointIndex);
    } else {
        Logger::log(NK_INFO, "Exiting Connect Waypoint mode.");
    }
    Menu::updateMenuState();
}

void Airg::handleDisconnectWaypointClicked() {
    disconnectWaypointModeEnabled = !disconnectWaypointModeEnabled;
    if (disconnectWaypointModeEnabled) {
        Logger::log(NK_INFO, "Entering Disconnect Waypoint mode. Start waypoint: %d", selectedWaypointIndex);
    } else {
        Logger::log(NK_INFO, "Exiting Disconnect Waypoint mode.");
    }
    Menu::updateMenuState();
}

void Airg::finalizeSave() {
    if (airgSaveState.size() == 2) {
        airgSaveState.clear();
    }
}

void Airg::connectWaypoints(const int startWaypointIndex, const int endWaypointIndex) {
    Waypoint& startWaypoint = reasoningGrid->m_WaypointList[startWaypointIndex];
    for (int direction = 0; direction < 8; ++direction) {
        if (startWaypoint.nNeighbors[direction] == endWaypointIndex) {
            Logger::log(NK_INFO, "Waypoints already connected.");
            return;
        }
    }
    connectWaypointModeEnabled = false;
    Menu::updateMenuState();
    if (startWaypointIndex == -1 || endWaypointIndex == -1) {
        Logger::log(NK_INFO, "Exiting Connect Waypoint mode.");
        return;
    }
    Waypoint& endWaypoint = reasoningGrid->m_WaypointList[endWaypointIndex];
    const Vec3 startPos = {startWaypoint.vPos.x, startWaypoint.vPos.y, startWaypoint.vPos.z};
    const Vec3 endPos = {endWaypoint.vPos.x, endWaypoint.vPos.y, endWaypoint.vPos.z};
    const Vec3 waypointDirectionVec = endPos - startPos;
    const Vec3 directionNormalized = waypointDirectionVec / waypointDirectionVec.GetMagnitude();
    float maxParallelization = -2;
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
        if (const float dot = directionNormalized.Dot(directionVec); dot > maxParallelization) {
            maxParallelization = dot;
            bestDirection = direction;
        }
    }
    startWaypoint.nNeighbors[bestDirection] = endWaypointIndex;
    endWaypoint.nNeighbors[(bestDirection + 4) % 8] = startWaypointIndex;
    Logger::log(NK_INFO,
        ("Connected waypoints: " + std::to_string(startWaypointIndex) + " and " + std::to_string(endWaypointIndex))
            .c_str());
    airgDirty = true;
    Logger::log(NK_INFO, "Exiting Connect Waypoint mode.");
}

void Airg::disconnectWaypoints(const int startWaypointIndex, const int endWaypointIndex) {
    Waypoint& startWaypoint = reasoningGrid->m_WaypointList[startWaypointIndex];
    bool connected = false;
    for (int direction = 0; direction < 8; ++direction) {
        if (startWaypoint.nNeighbors[direction] == endWaypointIndex) {
            startWaypoint.nNeighbors[direction] = -1;
            connected = true;
        }
    }
    disconnectWaypointModeEnabled = false;
    if (!connected) {
        Logger::log(NK_INFO, "Waypoints already disconnected.");
        return;
    }
    Waypoint& endWaypoint = reasoningGrid->m_WaypointList[endWaypointIndex];

    for (int direction = 0; direction < 8; ++direction) {
        if (endWaypoint.nNeighbors[direction] == startWaypointIndex) {
            endWaypoint.nNeighbors[direction] = -1;
        }
    }
    Menu::updateMenuState();
    if (startWaypointIndex == -1 || endWaypointIndex == -1) {
        Logger::log(NK_INFO, "Exiting Disconnect Waypoint mode.");
        return;
    }
    Logger::log(NK_INFO,
        ("Disconnected waypoints: " + std::to_string(startWaypointIndex) + " and " + std::to_string(endWaypointIndex))
            .c_str());
    airgDirty = true;
    Logger::log(NK_INFO, "Exiting Disconnect Waypoint mode.");
}

char* Airg::openAirgFileDialog() {
    nfdu8filteritem_t filter = {"Airg files", "airg"};
    return FileUtil::openNfdLoadDialog(&filter, 1);
}

char* Airg::openSaveAirgFileDialog() {
    nfdu8filteritem_t filter = {"Airg files", "airg"};
    return FileUtil::openNfdSaveDialog(&filter, 1, "output");
}

void Airg::addWaypointGeometry(std::vector<AirgVertex>& triVerts, std::vector<AirgVertex>& lineVerts,
    const Waypoint& waypoint, const bool selected, const glm::vec4& color, const bool forceFan) {
    constexpr float r = 0.1f;
    constexpr float zRenderOffset = 0.53f;
    const float z = waypoint.vPos.z + zRenderOffset;
    const glm::vec3 center(waypoint.vPos.x, z, -waypoint.vPos.y);
    constexpr glm::vec3 normal(0.0f, 1.0f, 0.0f);
    constexpr float step = std::numbers::pi_v<float> / 4.0f;

    if (selected || forceFan) {
        for (int i = 0; i < 8; i++) {
            const float a1 = static_cast<float>(i) * step;
            const float a2 = static_cast<float>(i + 1) * step;

            const glm::vec3 p1(waypoint.vPos.x + cosf(a1) * r, z, -(waypoint.vPos.y + sinf(a1) * r));
            const glm::vec3 p2(waypoint.vPos.x + cosf(a2) * r, z, -(waypoint.vPos.y + sinf(a2) * r));

            triVerts.push_back({center, normal, color});
            triVerts.push_back({p2, normal, color});
            triVerts.push_back({p1, normal, color});
        }
    } else {
        // Line Loop
        for (int i = 0; i < 8; i++) {
            const float a1 = static_cast<float>(i) / 8.0f * std::numbers::pi * 2;
            const float a2 = static_cast<float>(i + 1) / 8.0f * std::numbers::pi * 2;

            const glm::vec3 p1(waypoint.vPos.x + cosf(a1) * r, z, -(waypoint.vPos.y + sinf(a1) * r));
            const glm::vec3 p2(waypoint.vPos.x + cosf(a2) * r, z, -(waypoint.vPos.y + sinf(a2) * r));

            lineVerts.push_back({p1, glm::vec3(0, 1, 0), color});
            lineVerts.push_back({p2, glm::vec3(0, 1, 0), color});
        }
    }
}

int Airg::visibilityDataSize(const ReasoningGrid* reasoningGrid, const int waypointIndex) {
    const int offset1 = reasoningGrid->m_WaypointList[waypointIndex].nVisionDataOffset;
    int offset2;
    if (waypointIndex < (reasoningGrid->m_WaypointList.size() - 1)) {
        offset2 = reasoningGrid->m_WaypointList[waypointIndex + 1].nVisionDataOffset;
    } else {
        offset2 = reasoningGrid->m_pVisibilityData.size();
    }
    return offset2 - offset1;
}

void Airg::buildAirg(Airg* airg) {
    GridGenerator::getInstance().build();
    airg->airgBuilding = false;
    airg->airgLoaded = true;
    airgDirty = true;
    airgDirty = true;
    Menu::updateMenuState();
}

void Airg::renderAirg() {
    if (selectedWaypointIndex >= reasoningGrid->m_WaypointList.size()) {
        selectedWaypointIndex = -1;
    }

    static int lastSelectedWaypoint = -1;
    static int lastCellColorSource = -1;
    static float lastSpacing = -1.0f;
    static float lastXOffset = -1.0f;
    static float lastYOffset = -1.0f;

    if (Grid& grid = Grid::getInstance(); selectedWaypointIndex != lastSelectedWaypoint ||
        cellColorSource != lastCellColorSource || grid.spacing != lastSpacing || grid.xOffset != lastXOffset ||
        grid.yOffset != lastYOffset) {
        airgDirty = true;
        lastSelectedWaypoint = selectedWaypointIndex;
        lastCellColorSource = cellColorSource;
        lastSpacing = grid.spacing;
        lastXOffset = grid.xOffset;
        lastYOffset = grid.yOffset;
    }

    if (airgDirty) {
        std::vector<AirgVertex> triVerts;
        std::vector<AirgVertex> lineVerts;
        for (int i = 0; i < reasoningGrid->m_WaypointList.size(); i++) {
            constexpr float zRenderOffset = 0.52f;
            const Waypoint& waypoint = reasoningGrid->m_WaypointList[i];
            bool selected = (i == selectedWaypointIndex);

            // Grid Cells Logic
            if (cellColorSource != OFF) {
                float minX = reasoningGrid->m_Properties.vMin.x;
                float minY = reasoningGrid->m_Properties.vMin.y;
                const float cellSpacing = reasoningGrid->m_Properties.fGridSpacing;
                const int cellX = static_cast<int>(std::floor((waypoint.vPos.x - minX) / cellSpacing));
                const int cellY = static_cast<int>(std::floor((waypoint.vPos.y - minY) / cellSpacing));
                float x = cellX * cellSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 + minX;
                float y = cellY * cellSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 + minY;
                float z = waypoint.vPos.z + 0.01f;

                // Boundary
                glm::vec4 boundaryColor(0.8, 0.8, 0.8, 0.6);
                glm::vec3 p1(x - reasoningGrid->m_Properties.fGridSpacing / 2, z + zRenderOffset,
                    -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
                glm::vec3 p2(x - reasoningGrid->m_Properties.fGridSpacing / 2, z + zRenderOffset,
                    -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
                glm::vec3 p3(x + reasoningGrid->m_Properties.fGridSpacing / 2, z + zRenderOffset,
                    -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
                glm::vec3 p4(x + reasoningGrid->m_Properties.fGridSpacing / 2, z + zRenderOffset,
                    -(y - reasoningGrid->m_Properties.fGridSpacing / 2));

                lineVerts.push_back({p1, glm::vec3(0, 1, 0), boundaryColor});
                lineVerts.push_back({p2, glm::vec3(0, 1, 0), boundaryColor});
                lineVerts.push_back({p2, glm::vec3(0, 1, 0), boundaryColor});
                lineVerts.push_back({p3, glm::vec3(0, 1, 0), boundaryColor});
                lineVerts.push_back({p3, glm::vec3(0, 1, 0), boundaryColor});
                lineVerts.push_back({p4, glm::vec3(0, 1, 0), boundaryColor});
                lineVerts.push_back({p4, glm::vec3(0, 1, 0), boundaryColor});
                lineVerts.push_back({p1, glm::vec3(0, 1, 0), boundaryColor});

                if (cellColorSource == VISION_DATA) {
                    const unsigned int colorRgb = (waypoint.nLayerIndex << 6) | 0x80000000;
                    unsigned char a = (colorRgb >> 24) & 0xFF;
                    unsigned char b = (colorRgb >> 16) & 0xFF;
                    unsigned char g = (colorRgb >> 8) & 0xFF;
                    unsigned char r = colorRgb & 0xFF;
                    glm::vec4 color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);

                    triVerts.push_back({p1, glm::vec3(0, 1, 0), color});
                    triVerts.push_back({p2, glm::vec3(0, 1, 0), color});
                    triVerts.push_back({p3, glm::vec3(0, 1, 0), color});
                    triVerts.push_back({p1, glm::vec3(0, 1, 0), color});
                    triVerts.push_back({p3, glm::vec3(0, 1, 0), color});
                    triVerts.push_back({p4, glm::vec3(0, 1, 0), color});
                } else {
                    std::vector<uint8_t> data;
                    float size = 0;
                    if (cellColorSource == AIRG_BITMAP) {
                        size = 25;
                        for (int k = 0; k < 25; k++) {
                            data.push_back(waypoint.cellBitmap[k] * 255);
                        }
                    } else if (cellColorSource == LAYER) { // Render Vision Data
                        size = visibilityDataSize(reasoningGrid, i);
                        data = reasoningGrid->getWaypointVisionData(i);
                    }

                    float bw = reasoningGrid->m_Properties.fGridSpacing / sqrt(size);
                    int numBoxesPerSide = reasoningGrid->m_Properties.fGridSpacing / bw;
                    for (int byi = 0; byi < numBoxesPerSide; byi++) {
                        for (int bxi = 0; bxi < numBoxesPerSide; bxi++) {
                            float bx = x - reasoningGrid->m_Properties.fGridSpacing / 2 + bxi * bw;
                            float byRaw = (cellColorSource == AIRG_BITMAP)
                                ? -(y - reasoningGrid->m_Properties.fGridSpacing / 2 + (byi + 1) * bw)
                                : -y - reasoningGrid->m_Properties.fGridSpacing / 2 + byi * bw;

                            uint8_t val = data[numBoxesPerSide * byi + bxi];
                            float c = val / 255.0f;
                            glm::vec4 cellColor = (cellColorSource == AIRG_BITMAP) ? glm::vec4(c, c, c, 0.3)
                                                                                   : glm::vec4(0.0, c, 0.0, 0.3);

                            float zChange = selected ? 0.001 : 0.01;
                            float zc = waypoint.vPos.z + zChange;

                            glm::vec3 cp1(bx, zc + zRenderOffset, byRaw);
                            glm::vec3 cp2(bx, zc + zRenderOffset, byRaw + bw);
                            glm::vec3 cp3(bx + bw, zc + zRenderOffset, byRaw + bw);
                            glm::vec3 cp4(bx + bw, zc + zRenderOffset, byRaw);

                            triVerts.push_back({cp1, glm::vec3(0, 1, 0), cellColor});
                            triVerts.push_back({cp2, glm::vec3(0, 1, 0), cellColor});
                            triVerts.push_back({cp3, glm::vec3(0, 1, 0), cellColor});
                            triVerts.push_back({cp1, glm::vec3(0, 1, 0), cellColor});
                            triVerts.push_back({cp3, glm::vec3(0, 1, 0), cellColor});
                            triVerts.push_back({cp4, glm::vec3(0, 1, 0), cellColor});
                        }
                    }
                }
            }

            // Waypoint
            glm::vec4 wpColor(0, 0, 1, 0.5);
            addWaypointGeometry(triVerts, lineVerts, waypoint, selected, wpColor);

            // Neighbors
            for (int neighborIndex = 0; neighborIndex < waypoint.nNeighbors.size(); neighborIndex++) {
                if (waypoint.nNeighbors[neighborIndex] != 65535 &&
                    waypoint.nNeighbors[neighborIndex] < reasoningGrid->m_WaypointList.size()) {
                    const Waypoint& neighbor = reasoningGrid->m_WaypointList[waypoint.nNeighbors[neighborIndex]];
                    glm::vec3 pStart(waypoint.vPos.x, waypoint.vPos.z + zRenderOffset + 0.01f, -waypoint.vPos.y);
                    glm::vec3 pEnd(neighbor.vPos.x, neighbor.vPos.z + zRenderOffset + 0.01f, -neighbor.vPos.y);
                    lineVerts.push_back({pStart, glm::vec3(0, 1, 0), wpColor});
                    lineVerts.push_back({pEnd, glm::vec3(0, 1, 0), wpColor});
                }
            }
        }

        if (airgTriVao == 0) {
            glGenVertexArrays(1, &airgTriVao);
        }
        if (airgTriVbo == 0) {
            glGenBuffers(1, &airgTriVbo);
        }
        glBindVertexArray(airgTriVao);
        glBindBuffer(GL_ARRAY_BUFFER, airgTriVbo);
        glBufferData(GL_ARRAY_BUFFER, triVerts.size() * sizeof(AirgVertex), triVerts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, color));
        airgTriCount = triVerts.size();

        if (airgLineVao == 0) {
            glGenVertexArrays(1, &airgLineVao);
        }
        if (airgLineVbo == 0) {
            glGenBuffers(1, &airgLineVbo);
        }
        glBindVertexArray(airgLineVao);
        glBindBuffer(GL_ARRAY_BUFFER, airgLineVbo);
        glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(AirgVertex), lineVerts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, color));
        airgLineCount = lineVerts.size();

        glBindVertexArray(0);
        airgDirty = false;
    }

    Renderer& renderer = Renderer::getInstance();
    renderer.shader.use();
    renderer.shader.setMat4("view", renderer.view);
    renderer.shader.setMat4("projection", renderer.projection);
    renderer.shader.setMat4("model", glm::mat4(1.0f));
    renderer.shader.setBool("useFlatColor", true);
    renderer.shader.setBool("useVertexColor", true);

    if (airgTriCount > 0) {
        glBindVertexArray(airgTriVao);
        glDrawArrays(GL_TRIANGLES, 0, airgTriCount);
    }
    if (airgLineCount > 0) {
        glBindVertexArray(airgLineVao);
        glDrawArrays(GL_LINES, 0, airgLineCount);
    }
    glBindVertexArray(0);
    renderer.shader.setBool("useVertexColor", false);

    if (showAirgIndices) {
        std::vector<WorldTextLabel> labels;
        const size_t numWaypoints = reasoningGrid->m_WaypointList.size();
        labels.reserve(numWaypoints);
        for (size_t i = 0; i < numWaypoints; i++) {
            const Waypoint& waypoint = reasoningGrid->m_WaypointList[i];
            const glm::vec3 position(waypoint.vPos.x, waypoint.vPos.z + 0.1f, -waypoint.vPos.y);
            if (imguiRenderGLIsWorldTextInRange(position)) {
                labels.push_back({std::to_string(i), position, {1.0f, 0.7f, 0.7f}, 20.0f});
            }
        }
        imguiRenderGLDrawWorldTextBatch(labels);
    }
}

void Airg::renderAirgForHitTest() const {
    if (showAirg && airgLoaded) {
        std::vector<AirgVertex> triVerts;
        std::vector<AirgVertex> lineVerts; // Unused for hit test, but needed for helper

        const int numWaypoints = reasoningGrid->m_WaypointList.size();
        for (size_t i = 0; i < numWaypoints; i++) {
            const Waypoint& waypoint = reasoningGrid->m_WaypointList[i];
            const float r = static_cast<float>(AIRG_WAYPOINT) / 255.0f;
            const float g = static_cast<float>(i >> 8 & 0xFF) / 255.0f;
            const float b = static_cast<float>(i & 0xFF) / 255.0f;
            glm::vec4 color(r, g, b, 1.0f);
            addWaypointGeometry(triVerts, lineVerts, waypoint, true, color, true);
        }

        if (airgHitTestVao == 0) {
            glGenVertexArrays(1, &airgHitTestVao);
        }
        if (airgHitTestVbo == 0) {
            glGenBuffers(1, &airgHitTestVbo);
        }
        glBindVertexArray(airgHitTestVao);
        glBindBuffer(GL_ARRAY_BUFFER, airgHitTestVbo);
        glBufferData(GL_ARRAY_BUFFER, triVerts.size() * sizeof(AirgVertex), triVerts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, color));
        airgHitTestCount = triVerts.size();
        glBindVertexArray(0);

        const Renderer& renderer = Renderer::getInstance();
        renderer.shader.use();
        renderer.shader.setMat4("view", renderer.view);
        renderer.shader.setMat4("projection", renderer.projection);
        renderer.shader.setMat4("model", glm::mat4(1.0f));
        renderer.shader.setBool("useFlatColor", true);
        renderer.shader.setBool("useVertexColor", true);

        glBindVertexArray(airgHitTestVao);
        glDrawArrays(GL_TRIANGLES, 0, airgHitTestCount);
        glBindVertexArray(0);
        renderer.shader.setBool("useVertexColor", false);
    }
}

void Airg::setSelectedAirgWaypointIndex(const int index) {
    if (connectWaypointModeEnabled) {
        connectWaypointModeEnabled = false;
        Logger::log(NK_INFO, "Exiting Connect Waypoint mode.");
    }
    if (index == -1 && selectedWaypointIndex != -1) {
        Logger::log(NK_INFO, ("Deselected waypoint: " + std::to_string(selectedWaypointIndex)).c_str());
    }
    airgDirty = true;
    selectedWaypointIndex = index;
    if (index != -1 && index < reasoningGrid->m_WaypointList.size()) {
        const Waypoint waypoint = reasoningGrid->m_WaypointList[index];
        std::string msg = "Selected Airg Waypoint " + std::to_string(index);
        for (int i = 0; i < waypoint.nNeighbors.size(); i++) {
            const int neighborIndex = waypoint.nNeighbors[i];
            if (neighborIndex != 65535) {
                msg += ":  Neighbor " + std::to_string(i) + ": " + std::to_string(neighborIndex);
            }
        }
        Logger::log(NK_INFO, msg.c_str());
        Logger::log(NK_INFO,
            ("Waypoint position: X: " + std::to_string(waypoint.vPos.x) + " Y: " + std::to_string(waypoint.vPos.y) +
                " Z: " + std::to_string(waypoint.vPos.z) + "   XI: " + std::to_string(waypoint.xi) +
                " YI: " + std::to_string(waypoint.yi) + " ZI: " + std::to_string(waypoint.zi))
                .c_str());
        msg = "  Vision Data Offset: " + std::to_string(waypoint.nVisionDataOffset);
        msg += "  Layer Index: " + std::to_string(waypoint.nLayerIndex);
        const int nextWaypointOffset = (index + 1) < reasoningGrid->m_WaypointList.size()
            ? reasoningGrid->m_WaypointList[index + 1].nVisionDataOffset
            : reasoningGrid->m_pVisibilityData.size();
        const int visibilityDataSize = nextWaypointOffset - waypoint.nVisionDataOffset;
        msg += "  Visibility Data size: " + std::to_string(visibilityDataSize);

        const std::vector<uint8_t> waypointVisibilityData = reasoningGrid->getWaypointVisionData(index);

        if (!waypointVisibilityData.empty()) {
            std::string waypointVisibilityDataString;
            char numHex[3];
            msg += "  Visibility Data Type: " + std::to_string(waypointVisibilityData[0]); // +" Visibility data:";
            Logger::log(NK_INFO, msg.c_str());
            for (int count = 2; count < waypointVisibilityData.size(); count++) {
                const uint8_t num = waypointVisibilityData[count];
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
                    // Logger::log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
                    waypointVisibilityDataString = "";
                }
            }
        }
        // Logger::log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
        const unsigned int colorRgb = (waypoint.nLayerIndex << 6) | 0xC0000000;
        const std::string hexColor = std::format("{:x}", colorRgb);
        Logger::log(NK_INFO, ("Layer Index RGB " + hexColor).c_str());
    }
    Menu::updateMenuState();
}

void Airg::saveAirg(Airg* airg, const std::string& fileName) {
    airg->airgSaveState.push_back(true);
    try {
        const std::time_t startTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::string msg = "Saving Airg to file at ";
        msg += std::ctime(&startTime);
        Logger::log(NK_INFO, msg.data());
        const auto start = std::chrono::high_resolution_clock::now();

        airg->reasoningGrid->writeAirg(fileName);
        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        msg = "Finished saving Airg to " + fileName + " in ";
        msg += std::to_string(duration.count());
        msg += " seconds";
        Logger::log(NK_INFO, msg.data());
    } catch (const std::exception& error) {
        Logger::log(NK_ERROR, ("Failed to save AIRG file: " + std::string(error.what())).c_str());
    } catch (...) {
        Logger::log(NK_ERROR, "Failed to save AIRG file due to an unknown error.");
    }
    airg->airgSaveState.push_back(true);
}

void Airg::loadAirg(Airg* airg, const std::string& fileName) {
    airg->airgLoading = true;
    airg->airgLoaded = false;

    Menu::updateMenuState();
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Airg from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    const auto start = std::chrono::high_resolution_clock::now();

    try {
        airg->reasoningGrid->readAirg(fileName);
        if (airg->reasoningGrid->m_WaypointList.empty()) {
            throw std::runtime_error("AIRG file contains no waypoints.");
        }
        uint32_t previousOffset = 0;
        for (const Waypoint& waypoint : airg->reasoningGrid->m_WaypointList) {
            if (waypoint.nVisionDataOffset < previousOffset ||
                waypoint.nVisionDataOffset > airg->reasoningGrid->m_pVisibilityData.size()) {
                throw std::runtime_error("AIRG waypoint visibility offsets are invalid.");
            }
            previousOffset = waypoint.nVisionDataOffset;
        }
    } catch (const std::exception& error) {
        airg->airgLoading = false;
        airg->airgLoaded = false;
        Logger::log(NK_ERROR, ("Failed to load AIRG file: " + std::string(error.what())).c_str());
        Menu::updateMenuState();
        return;
    } catch (...) {
        airg->airgLoading = false;
        airg->airgLoaded = false;
        Logger::log(NK_ERROR, "Failed to load AIRG file due to an unknown error.");
        Menu::updateMenuState();
        return;
    }
    Grid::getInstance().saveSpacing(airg->reasoningGrid->m_Properties.fGridSpacing);

    int lastVisionDataSize = 0;
    int count = 1;
    int total = 0;
    std::map<int, int> visionDataOffsetCounts;
    for (int i = 1; i < airg->reasoningGrid->m_WaypointList.size(); i++) {
        const Waypoint w1 = airg->reasoningGrid->m_WaypointList[i - 1];
        const Waypoint w2 = airg->reasoningGrid->m_WaypointList[i];
        int visionDataSize = w2.nVisionDataOffset - w1.nVisionDataOffset;
        total += visionDataSize;
        if (lastVisionDataSize != visionDataSize) {
            lastVisionDataSize = visionDataSize;
            Logger::log(NK_INFO,
                ("Vision Data Offset[" + std::to_string(i - 1) + "]: " + std::to_string(w1.nVisionDataOffset) +
                    " Vision Data Offset[" + std::to_string(i) + "]: " + std::to_string(w2.nVisionDataOffset) +
                    " Difference : " + std::to_string(lastVisionDataSize) + " Count: " + std::to_string(count))
                    .c_str());
            if (visionDataOffsetCounts.contains(visionDataSize)) {
                const int cur = visionDataOffsetCounts[visionDataSize];
                visionDataOffsetCounts[visionDataSize] = cur + 1;
            } else {
                visionDataOffsetCounts[visionDataSize] = 1;
            }
            count = 1;
        } else {
            count++;
        }
    }
    const int finalVisionDataSize = airg->reasoningGrid->m_pVisibilityData.size() -
        airg->reasoningGrid->m_WaypointList[airg->reasoningGrid->m_WaypointList.size() - 1].nVisionDataOffset;
    Logger::log(NK_DEBUG,
        ("Vision Data Offset[" + std::to_string(airg->reasoningGrid->m_WaypointList.size() - 1) + "]: " +
            std::to_string(
                airg->reasoningGrid->m_WaypointList[airg->reasoningGrid->m_WaypointList.size() - 1].nVisionDataOffset) +
            " Max Visibility: " + std::to_string(airg->reasoningGrid->m_pVisibilityData.size()) +
            " Difference : " + std::to_string(finalVisionDataSize))
            .c_str());
    total += finalVisionDataSize;
    Logger::log(NK_DEBUG,
        ("Total: " + std::to_string(total) +
            " Max Visibility: " + std::to_string(airg->reasoningGrid->m_pVisibilityData.size()))
            .c_str());
    Logger::log(NK_DEBUG, "Visibility data offset map:");
    for (const auto& pair : visionDataOffsetCounts) {
        VisionData visionData = VisionData::GetVisionDataType(pair.first);
        Logger::log(NK_DEBUG,
            ("Offset difference: " + std::to_string(pair.first) + " Color: " + visionData.getName() +
                " Count: " + std::to_string(pair.second))
                .c_str());
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished loading Airg in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    Logger::log(NK_INFO, msg.data());
    Logger::log(NK_INFO,
        ("Waypoint count: " + std::to_string(airg->reasoningGrid->m_WaypointList.size()) + ", Visibility Data size: " +
            std::to_string(airg->reasoningGrid->m_pVisibilityData.size()) + ", Visibility Data points per waypoint: " +
            std::to_string(static_cast<double>(airg->reasoningGrid->m_pVisibilityData.size()) /
                static_cast<double>(airg->reasoningGrid->m_WaypointList.size())))
            .c_str());
    airg->airgLoading = false;
    airg->airgLoaded = true;
    airgDirty = true;
    airgDirty = true;
    Grid::getInstance().loadBoundsFromAirg();
    Menu::updateMenuState();
}

void Airg::updateAirgDialogControls(wxDialog* dialog) {
    auto* choice = static_cast<wxChoice*>(dialog->FindWindow(IDC_COMBOBOX_AIRG));
    choice->Clear();
    std::vector<std::pair<std::string, std::string>> entries(airgHashIoiStringMap.begin(), airgHashIoiStringMap.end());
    std::ranges::sort(entries,
        [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
    for (const auto& [hash, ioiString] : entries) {
        const std::string& display = ioiString.empty() ? hash : ioiString;
        choice->Append(wxString::FromUTF8(display));
        if (selectedRpkgAirg.empty()) {
            selectedRpkgAirg = display;
        }
    }
    if (!entries.empty()) {
        choice->SetSelection(0);
    }
}

void Airg::extractAirgFromRpkgs(const std::string& hash) {
    if (!Rpkg::extractResourcesFromRpkgs({hash}, AIRG)) {
        const std::string fileName =
            (std::filesystem::u8path(NavKitSettings::getInstance().outputFolder) / "airg" / (hash + ".AIRG")).string();
        Logger::log(NK_INFO, ("Loading airg from file: " + fileName).c_str());
        getInstance().loadAirgFromFile(fileName);
    }
}

void Airg::showExtractAirgDialog() {
    if (hExtractAirgDialog) {
        hExtractAirgDialog->Raise();
        return;
    }
    auto* dialog =
        new wxDialog(getMainFrame(), wxID_ANY, "Load Airg from resource package", wxDefaultPosition, wxSize(500, 150));
    hExtractAirgDialog = dialog;
    auto* choice = new wxChoice(dialog, IDC_COMBOBOX_AIRG);
    updateAirgDialogControls(dialog);
    auto* buttons = new wxStdDialogButtonSizer();
    auto* open = new wxButton(dialog, wxID_OK, "Open Airg");
    auto* cancel = new wxButton(dialog, wxID_CANCEL, "Cancel");
    buttons->AddButton(open);
    buttons->AddButton(cancel);
    buttons->Realize();
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(
        new wxStaticText(dialog, wxID_ANY, "Airg file in resource packages (Rpkg) to extract and load"), 0, wxALL, 10);
    sizer->Add(choice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxALL, 10);
    dialog->SetSizer(sizer);
    choice->Bind(wxEVT_CHOICE, [](wxCommandEvent& event) { selectedRpkgAirg = event.GetString().ToStdString(); });
    open->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) {
        for (auto& [hash, ioiString] : airgHashIoiStringMap) {
            if (hash == selectedRpkgAirg) {
                getInstance().loadedAirgText = hash;
            } else if (ioiString == selectedRpkgAirg) {
                getInstance().loadedAirgText = ioiString;
            } else {
                continue;
            }
            extractAirgFromRpkgs(hash);
        }
        dialog->Close();
    });
    cancel->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) { dialog->Close(); });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hExtractAirgDialog == dialog) {
            hExtractAirgDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
}
