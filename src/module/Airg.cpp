#include "../../include/NavKit/module/Airg.h"

#include <filesystem>
#include <fstream>
#include <numbers>

#include <CommCtrl.h>
#include <iomanip>
#include <SDL.h>
#include <sstream>
#include <string>
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/model/VisionData.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/GridGenerator.h"
#include "../../include/ResourceLib_HM3/ResourceConverter.h"
#include "../../include/ResourceLib_HM3/ResourceGenerator.h"
#include "../../include/ResourceLib_HM3/ResourceLib_HM3.h"
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
      , selectedWaypointIndex(-1)
      , doAirgHitTest(false)
      , buildingVisionAndDeadEndData(false) {
}

Airg::~Airg() = default;

HWND Airg::hAirgDialog = nullptr;
std::string Airg::selectedRpkgAirg{};
std::map<std::string, std::string> Airg::airgHashIoiStringMap;


GLuint Airg::airgTriVAO = 0;
GLuint Airg::airgTriVBO = 0;
GLuint Airg::airgLineVAO = 0;
GLuint Airg::airgLineVBO = 0;
int Airg::airgTriCount = 0;
int Airg::airgLineCount = 0;
bool Airg::airgDirty = true;

GLuint Airg::airgHitTestVAO = 0;
GLuint Airg::airgHitTestVBO = 0;
int Airg::airgHitTestCount = 0;
bool Airg::airgHitTestDirty = true;

static std::string format_float(float val) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    return ss.str();
}

INT_PTR CALLBACK Airg::AirgDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    Grid& grid = Grid::getInstance();

    switch (message) {
    case WM_INITDIALOG: {
        UpdateDialogControls(hDlg);
        return (INT_PTR)TRUE;
    }

    case WM_HSCROLL: {
        float oldSpacing = grid.spacing;

        if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_SPACING)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            grid.spacing = 0.1f + (pos * 0.05f);
            SetDlgItemText(hDlg, IDC_STATIC_SPACING_VAL, format_float(grid.spacing).c_str());

            if (oldSpacing != grid.spacing) {
                grid.saveSpacing(grid.spacing);
                UpdateDialogControls(hDlg);
                airgDirty = true;
            }
        }
        else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_XOFFSET)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            grid.xOffset = -grid.spacing + (pos * 0.05f);
            SetDlgItemText(hDlg, IDC_STATIC_XOFFSET_VAL, format_float(grid.xOffset).c_str());
            airgDirty = true;
        }
        else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_ZOFFSET)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            grid.yOffset = -grid.spacing + (pos * 0.05f);
            SetDlgItemText(hDlg, IDC_STATIC_ZOFFSET_VAL, format_float(grid.yOffset).c_str());
            airgDirty = true;
        }
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BUTTON_RESET_DEFAULTS) {
            Logger::log(NK_INFO, "Resetting Airg Default settings");
            resetDefaults();
            UpdateDialogControls(hDlg);
            airgDirty = true;
        }
        return (INT_PTR)TRUE;
    }

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return (INT_PTR)TRUE;

    case WM_DESTROY:
        hAirgDialog = nullptr;
        return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

void Airg::showAirgDialog() {
    if (hAirgDialog) {
        SetForegroundWindow(hAirgDialog);
        return;
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    HWND hParentWnd = Renderer::hwnd;

    hAirgDialog = CreateDialogParam(
        hInstance,
        MAKEINTRESOURCE(IDD_AIRG_MENU),
        hParentWnd,
        AirgDialogProc,
        (LPARAM)this
    );

    if (hAirgDialog) {
        if (HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON))) {
            SendMessage(hAirgDialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessage(hAirgDialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }
        RECT parentRect, dialogRect;
        GetWindowRect(hParentWnd, &parentRect);
        GetWindowRect(hAirgDialog, &dialogRect);

        int parentWidth = parentRect.right - parentRect.left;
        int parentHeight = parentRect.bottom - parentRect.top;
        int dialogWidth = dialogRect.right - dialogRect.left;
        int dialogHeight = dialogRect.bottom - dialogRect.top;

        int newX = parentRect.left + (parentWidth - dialogWidth) / 2;
        int newY = parentRect.top + (parentHeight - dialogHeight) / 2;

        SetWindowPos(hAirgDialog, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ShowWindow(hAirgDialog, SW_SHOW);
    }
}

void Airg::UpdateDialogControls(HWND hDlg) {
    Grid& grid = Grid::getInstance();

    HWND hSliderSpacing = GetDlgItem(hDlg, IDC_SLIDER_SPACING);
    HWND hSliderX = GetDlgItem(hDlg, IDC_SLIDER_XOFFSET);
    HWND hSliderZ = GetDlgItem(hDlg, IDC_SLIDER_ZOFFSET);

    // --- Spacing Slider ---
    SendMessage(hSliderSpacing, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(0, 78));
    int spacing_pos = static_cast<int>((grid.spacing - 0.1f) / 0.05f);
    SendMessage(hSliderSpacing, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)spacing_pos);
    SetDlgItemText(hDlg, IDC_STATIC_SPACING_VAL, format_float(grid.spacing).c_str());

    // --- X and Z Offset Sliders ---
    int range = static_cast<int>((grid.spacing * 2) / 0.05f);

    // X Offset
    SendMessage(hSliderX, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(0, range));
    int x_pos = static_cast<int>((grid.xOffset + grid.spacing) / 0.05f);
    SendMessage(hSliderX, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)x_pos);
    SetDlgItemText(hDlg, IDC_STATIC_XOFFSET_VAL, format_float(grid.xOffset).c_str());

    // Z Offset
    SendMessage(hSliderZ, TBM_SETRANGE, (WPARAM)TRUE, (LPARAM)MAKELONG(0, range));
    int z_pos = static_cast<int>((grid.yOffset + grid.spacing) / 0.05f);
    SendMessage(hSliderZ, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)z_pos);
    SetDlgItemText(hDlg, IDC_STATIC_ZOFFSET_VAL, format_float(grid.yOffset).c_str());
}

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
        Menu::updateMenuState();
        airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
    }
}

void Airg::setLastSaveFileName(const char *fileName) {
    saveAirgName = fileName;
    lastSaveAirgFile = saveAirgName;
    saveAirgName = saveAirgName.substr(saveAirgName.find_last_of("/\\") + 1);
}

void Airg::loadAirgFromFile(const std::string& fileName) {
    setLastLoadFileName(fileName.c_str());
    std::string fileNameStr = fileName;
    std::string extension = airgName.substr(airgName.length() - 4, airgName.length());
    std::ranges::transform(extension, extension.begin(), ::toupper);

    if (extension == "JSON") {
        delete reasoningGrid;
        reasoningGrid = new ReasoningGrid();
        Logger::log(NK_INFO, "Loading Airg.Json file: '%s'...", fileNameStr.c_str());
        backgroundWorker.emplace(&Airg::loadAirg, this, fileNameStr, true);
    } else if (extension == "AIRG") {
        delete reasoningGrid;
        reasoningGrid = new ReasoningGrid();
        Logger::log(NK_INFO, "Loading Airg file: '%s'...", fileNameStr.c_str());
        backgroundWorker.emplace(&Airg::loadAirg, this, fileNameStr, false);
    }
    airgDirty = true;
}

void Airg::handleOpenAirgClicked() {
    if (const char *fileName = openAirgFileDialog(lastLoadAirgFile.data())) {
        loadedAirgText = fileName;
        loadAirgFromFile(fileName);
    }
}

void Airg::handleSaveAirgClicked() {
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
        backgroundWorker.emplace(&Airg::saveAirg, this, fileName, extension == "JSON");
    }
}

bool Airg::canLoad() const {
    return !airgLoading && airgSaveState.empty();
}

bool Airg::canSave() const {
    return airgLoaded && airgSaveState.empty();
}

bool Airg::canBuildAirg() const {
    const Navp &navp = Navp::getInstance();
    return navp.navpLoaded && !airgLoading && airgSaveState.empty() && !airgBuilding;
}

void Airg::handleBuildAirgClicked() {
    if (const Navp &navp = Navp::getInstance(); navp.navMesh->m_areas.size() > 65535) {
        Logger::log(
            NK_ERROR,
            "Loaded NAVP has too many areas. Ensure the scene has PF Seed Points and has a fully contained boundary.");
    }
    airgLoaded = false;
    Menu::updateMenuState();
    airgBuilding = true;
    delete reasoningGrid;
    reasoningGrid = new ReasoningGrid();
    Logger::log(NK_INFO, "Building Airg from Airg");
    build();
}

bool Airg::canEnterConnectWaypointMode() const {
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

void Airg::finalizeSave() {
    if (airgSaveState.size() == 2) {
        airgSaveState.clear();
    }
}

void Airg::connectWaypoints(const int startWaypointIndex, const int endWaypointIndex) {
    Waypoint &startWaypoint = reasoningGrid->m_WaypointList[startWaypointIndex];
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
    Waypoint &endWaypoint = reasoningGrid->m_WaypointList[endWaypointIndex];
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
                ("Connected waypoints: " + std::to_string(startWaypointIndex) + " and " + std::to_string(
                     endWaypointIndex)).c_str());
    Logger::log(NK_INFO, "Exiting Connect Waypoint mode.");
}

char *Airg::openAirgFileDialog(const char *lastAirgFolder) {
    nfdu8filteritem_t filters[2] = {{"Airg files", "airg"}, {"Airg.json files", "airg.json"}};
    return FileUtil::openNfdLoadDialog(filters, 2);
}

char *Airg::openSaveAirgFileDialog(char *lastAirgFolder) {
    nfdu8filteritem_t filters[2] = {{"Airg files", "airg"}, {"Airg.json files", "airg.json"}};
    return FileUtil::openNfdSaveDialog(filters, 2, "output");
}

void Airg::addWaypointGeometry(std::vector<AirgVertex>& triVerts, std::vector<AirgVertex>& lineVerts, const Waypoint& waypoint, bool selected, const glm::vec4& color, bool forceFan) {
    const float r = 0.1f;
    const float z = waypoint.vPos.z + 0.03f;
    glm::vec3 center(waypoint.vPos.x, z, -waypoint.vPos.y);

    if (selected || forceFan) {
        for (int i = 0; i < 8; i++) {
            float a1 = (float)i / 8.0f * std::numbers::pi * 2;
            float a2 = (float)(i + 1) / 8.0f * std::numbers::pi * 2;

            glm::vec3 p1(waypoint.vPos.x + cosf(a1) * r, z, -(waypoint.vPos.y + sinf(a1) * r));
            glm::vec3 p2(waypoint.vPos.x + cosf(a2) * r, z, -(waypoint.vPos.y + sinf(a2) * r));

            triVerts.push_back({center, glm::vec3(0,1,0), color});
            triVerts.push_back({p1, glm::vec3(0,1,0), color});
            triVerts.push_back({p2, glm::vec3(0,1,0), color});
        }
    } else {
        // Line Loop
        for (int i = 0; i < 8; i++) {
            float a1 = (float)i / 8.0f * std::numbers::pi * 2;
            float a2 = (float)(i + 1) / 8.0f * std::numbers::pi * 2;

            glm::vec3 p1(waypoint.vPos.x + cosf(a1) * r, z, -(waypoint.vPos.y + sinf(a1) * r));
            glm::vec3 p2(waypoint.vPos.x + cosf(a2) * r, z, -(waypoint.vPos.y + sinf(a2) * r));

            lineVerts.push_back({p1, glm::vec3(0,1,0), color});
            lineVerts.push_back({p2, glm::vec3(0,1,0), color});
        }
    }
}

int Airg::visibilityDataSize(ReasoningGrid *reasoningGrid, int waypointIndex) {
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
    backgroundWorker.emplace(&GridGenerator::build, &GridGenerator::getInstance());
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

    Grid& grid = Grid::getInstance();
    if (selectedWaypointIndex != lastSelectedWaypoint ||
        cellColorSource != lastCellColorSource ||
        grid.spacing != lastSpacing ||
        grid.xOffset != lastXOffset ||
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
            const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
            bool selected = (i == selectedWaypointIndex);

            // Grid Cells Logic
            if (cellColorSource != OFF) {
                float minX = reasoningGrid->m_Properties.vMin.x;
                float minY = reasoningGrid->m_Properties.vMin.y;
                float x = waypoint.xi * reasoningGrid->m_Properties.fGridSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 + minX;
                float y = waypoint.yi * reasoningGrid->m_Properties.fGridSpacing + reasoningGrid->m_Properties.fGridSpacing / 2 + minY;
                float z = waypoint.vPos.z + 0.01f;

                // Boundary
                glm::vec4 boundaryColor(0.8, 0.8, 0.8, 0.6);
                glm::vec3 p1(x - reasoningGrid->m_Properties.fGridSpacing / 2, z, -(y - reasoningGrid->m_Properties.fGridSpacing / 2));
                glm::vec3 p2(x - reasoningGrid->m_Properties.fGridSpacing / 2, z, -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
                glm::vec3 p3(x + reasoningGrid->m_Properties.fGridSpacing / 2, z, -(y + reasoningGrid->m_Properties.fGridSpacing / 2));
                glm::vec3 p4(x + reasoningGrid->m_Properties.fGridSpacing / 2, z, -(y - reasoningGrid->m_Properties.fGridSpacing / 2));

                lineVerts.push_back({p1, glm::vec3(0,1,0), boundaryColor}); lineVerts.push_back({p2, glm::vec3(0,1,0), boundaryColor});
                lineVerts.push_back({p2, glm::vec3(0,1,0), boundaryColor}); lineVerts.push_back({p3, glm::vec3(0,1,0), boundaryColor});
                lineVerts.push_back({p3, glm::vec3(0,1,0), boundaryColor}); lineVerts.push_back({p4, glm::vec3(0,1,0), boundaryColor});
                lineVerts.push_back({p4, glm::vec3(0,1,0), boundaryColor}); lineVerts.push_back({p1, glm::vec3(0,1,0), boundaryColor});

                if (cellColorSource == VISION_DATA) {
                    const unsigned int colorRgb = (waypoint.nLayerIndex << 6) | 0x80000000;
                    unsigned char a = (colorRgb >> 24) & 0xFF;
                    unsigned char b = (colorRgb >> 16) & 0xFF;
                    unsigned char g = (colorRgb >> 8) & 0xFF;
                    unsigned char r = colorRgb & 0xFF;
                    glm::vec4 color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);

                    triVerts.push_back({p1, glm::vec3(0,1,0), color}); triVerts.push_back({p2, glm::vec3(0,1,0), color}); triVerts.push_back({p3, glm::vec3(0,1,0), color});
                    triVerts.push_back({p1, glm::vec3(0,1,0), color}); triVerts.push_back({p3, glm::vec3(0,1,0), color}); triVerts.push_back({p4, glm::vec3(0,1,0), color});
                } else {
                    std::vector<uint8_t> data;
                    float size = 0;
                    if (cellColorSource == AIRG_BITMAP) {
                        size = 25;
                        for (int k = 0; k < 25; k++) data.push_back(waypoint.cellBitmap[k] * 255);
                    } else if (cellColorSource == LAYER) { // Render Vision Data
                        size = visibilityDataSize(reasoningGrid, i);
                        data = reasoningGrid->getWaypointVisionData(i);
                    }

                    float bw = reasoningGrid->m_Properties.fGridSpacing / sqrt(size);
                    int numBoxesPerSide = reasoningGrid->m_Properties.fGridSpacing / bw;
                    for (int byi = 0; byi < numBoxesPerSide; byi++) {
                        for (int bxi = 0; bxi < numBoxesPerSide; bxi++) {
                            float bx = x - reasoningGrid->m_Properties.fGridSpacing / 2 + bxi * bw;
                            float by_raw = (cellColorSource == AIRG_BITMAP) ?
                                           -(y - reasoningGrid->m_Properties.fGridSpacing / 2 + (byi + 1) * bw) :
                                           -y - reasoningGrid->m_Properties.fGridSpacing / 2 + byi * bw;

                            uint8_t val = data[numBoxesPerSide * byi + bxi];
                            float c = val / 255.0f;
                            glm::vec4 cellColor = (cellColorSource == AIRG_BITMAP) ? glm::vec4(c, c, c, 0.3) : glm::vec4(0.0, c, 0.0, 0.3);

                            float zChange = selected ? 0.001 : 0.01;
                            float zc = waypoint.vPos.z + zChange;

                            glm::vec3 cp1(bx, zc, by_raw);
                            glm::vec3 cp2(bx, zc, by_raw + bw);
                            glm::vec3 cp3(bx + bw, zc, by_raw + bw);
                            glm::vec3 cp4(bx + bw, zc, by_raw);

                            triVerts.push_back({cp1, glm::vec3(0,1,0), cellColor}); triVerts.push_back({cp2, glm::vec3(0,1,0), cellColor}); triVerts.push_back({cp3, glm::vec3(0,1,0), cellColor});
                            triVerts.push_back({cp1, glm::vec3(0,1,0), cellColor}); triVerts.push_back({cp3, glm::vec3(0,1,0), cellColor}); triVerts.push_back({cp4, glm::vec3(0,1,0), cellColor});
                        }
                    }
                }
            }

            // Waypoint
            glm::vec4 wpColor(0, 0, 1, 0.5);
            addWaypointGeometry(triVerts, lineVerts, waypoint, selected, wpColor);

            // Neighbors
            for (int neighborIndex = 0; neighborIndex < waypoint.nNeighbors.size(); neighborIndex++) {
                if (waypoint.nNeighbors[neighborIndex] != 65535 && waypoint.nNeighbors[neighborIndex] < reasoningGrid->m_WaypointList.size()) {
                    const Waypoint &neighbor = reasoningGrid->m_WaypointList[waypoint.nNeighbors[neighborIndex]];
                    glm::vec3 pStart(waypoint.vPos.x, waypoint.vPos.z + 0.01f, -waypoint.vPos.y);
                    glm::vec3 pEnd(neighbor.vPos.x, neighbor.vPos.z + 0.01f, -neighbor.vPos.y);
                    lineVerts.push_back({pStart, glm::vec3(0,1,0), wpColor});
                    lineVerts.push_back({pEnd, glm::vec3(0,1,0), wpColor});
                }
            }
        }

        if (airgTriVAO == 0) glGenVertexArrays(1, &airgTriVAO);
        if (airgTriVBO == 0) glGenBuffers(1, &airgTriVBO);
        glBindVertexArray(airgTriVAO);
        glBindBuffer(GL_ARRAY_BUFFER, airgTriVBO);
        glBufferData(GL_ARRAY_BUFFER, triVerts.size() * sizeof(AirgVertex), triVerts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, pos));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, normal));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, color));
        airgTriCount = triVerts.size();

        if (airgLineVAO == 0) glGenVertexArrays(1, &airgLineVAO);
        if (airgLineVBO == 0) glGenBuffers(1, &airgLineVBO);
        glBindVertexArray(airgLineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, airgLineVBO);
        glBufferData(GL_ARRAY_BUFFER, lineVerts.size() * sizeof(AirgVertex), lineVerts.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, pos));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, normal));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, color));
        airgLineCount = lineVerts.size();

        glBindVertexArray(0);
        airgDirty = false;
    }

    Renderer &renderer = Renderer::getInstance();
    renderer.shader.use();
    renderer.shader.setMat4("view", renderer.view);
    renderer.shader.setMat4("projection", renderer.projection);
    renderer.shader.setMat4("model", glm::mat4(1.0f));
    renderer.shader.setBool("useFlatColor", true);
    renderer.shader.setBool("useVertexColor", true);

    if (airgTriCount > 0) {
        glBindVertexArray(airgTriVAO);
        glDrawArrays(GL_TRIANGLES, 0, airgTriCount);
    }
    if (airgLineCount > 0) {
        glBindVertexArray(airgLineVAO);
        glDrawArrays(GL_LINES, 0, airgLineCount);
    }
    glBindVertexArray(0);
    renderer.shader.setBool("useVertexColor", false);

    if (showAirgIndices) {
        int numWaypoints = reasoningGrid->m_WaypointList.size();
        for (size_t i = 0; i < numWaypoints; i++) {
            const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
            renderer.drawText(std::to_string(i), {waypoint.vPos.x, waypoint.vPos.z + 0.1f, -waypoint.vPos.y},
                              {1, .7f, .7f});
        }
    }
}

void Airg::renderAirgForHitTest() const {
    if (showAirg && airgLoaded) {
        if (airgHitTestDirty) {
            std::vector<AirgVertex> triVerts;
            std::vector<AirgVertex> lineVerts; // Unused for hit test, but needed for helper

            const int numWaypoints = reasoningGrid->m_WaypointList.size();
            for (size_t i = 0; i < numWaypoints; i++) {
                const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
                float r = (float)AIRG_WAYPOINT / 255.0f;
                float g = (float)(i / 255) / 255.0f;
                float b = (float)(i % 255) / 255.0f;
                glm::vec4 color(r, g, b, 1.0f);
                addWaypointGeometry(triVerts, lineVerts, waypoint, true, color, true);
            }

            if (airgHitTestVAO == 0) glGenVertexArrays(1, &airgHitTestVAO);
            if (airgHitTestVBO == 0) glGenBuffers(1, &airgHitTestVBO);
            glBindVertexArray(airgHitTestVAO);
            glBindBuffer(GL_ARRAY_BUFFER, airgHitTestVBO);
            glBufferData(GL_ARRAY_BUFFER, triVerts.size() * sizeof(AirgVertex), triVerts.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, pos));
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, normal));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(AirgVertex), (void*)offsetof(AirgVertex, color));
            airgHitTestCount = triVerts.size();
            glBindVertexArray(0);
            airgHitTestDirty = false;
        }

        Renderer &renderer = Renderer::getInstance();
        renderer.shader.use();
        renderer.shader.setMat4("view", renderer.view);
        renderer.shader.setMat4("projection", renderer.projection);
        renderer.shader.setMat4("model", glm::mat4(1.0f));
        renderer.shader.setBool("useFlatColor", true);
        renderer.shader.setBool("useVertexColor", true);

        glBindVertexArray(airgHitTestVAO);
        glDrawArrays(GL_TRIANGLES, 0, airgHitTestCount);
        glBindVertexArray(0);
        renderer.shader.setBool("useVertexColor", false);
    }
}

void Airg::setSelectedAirgWaypointIndex(int index) {
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
    Menu::updateMenuState();
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

void Airg::loadAirg(Airg *airg, const std::string& fileName, bool isFromJson) {
    airg->airgLoading = true;
    airg->airgLoaded = false;

    Menu::updateMenuState();
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Airg from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();

    std::string jsonFileName = fileName;
    if (!isFromJson) {
        std::string nameWithoutExtension = jsonFileName.substr(0, jsonFileName.length() - 5);

        jsonFileName = nameWithoutExtension + ".temp.airg.json";
        airg->airgResourceConverter->FromResourceFileToJsonFile(fileName.data(), jsonFileName.data());
    }
    airg->reasoningGrid->readJson(jsonFileName.data());
    Grid::getInstance().saveSpacing(airg->reasoningGrid->m_Properties.fGridSpacing);
    if (!isFromJson) {
        std::filesystem::remove(jsonFileName);
    }
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
    Logger::log(NK_DEBUG,
                ("Vision Data Offset[" + std::to_string(airg->reasoningGrid->m_WaypointList.size() - 1) + "]: " +
                 std::to_string(
                     airg->reasoningGrid->m_WaypointList[airg->reasoningGrid->m_WaypointList.size() - 1].
                     nVisionDataOffset) + " Max Visibility: " + std::to_string(
                     airg->reasoningGrid->m_pVisibilityData.size()) + " Difference : " + std::to_string(
                     finalVisionDataSize)).c_str());
    total += finalVisionDataSize;
    Logger::log(NK_DEBUG,
                ("Total: " + std::to_string(total) + " Max Visibility: " + std::to_string(
                     airg->reasoningGrid->m_pVisibilityData.size())).c_str());
    Logger::log(NK_DEBUG, "Visibility data offset map:");
    for (auto &pair: visionDataOffsetCounts) {
        VisionData visionData = VisionData::GetVisionDataType(pair.first);
        Logger::log(NK_DEBUG,
                    ("Offset difference: " + std::to_string(pair.first) + " Color: " + visionData.getName() +
                     " Count: " + std::to_string(pair.second)).c_str());
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
    airg->airgLoading = false;
    airg->airgLoaded = true;
    airgDirty = true;
    airgHitTestDirty = true;
    Grid::getInstance().loadBoundsFromAirg();
    Menu::updateMenuState();
}

void Airg::updateAirgDialogControls(HWND hwnd) {
    auto hWndComboBox = GetDlgItem(hwnd, IDC_COMBOBOX_AIRG);
    SendMessage(hWndComboBox, CB_RESETCONTENT, 0, 0);

    if (!airgHashIoiStringMap.empty()) {
        std::vector<std::pair<std::string, std::string>> sorted_hash_ioi_string_pairs(airgHashIoiStringMap.begin(), airgHashIoiStringMap.end());
        auto comparator = [](const std::pair<std::string, std::string>& a,
                             const std::pair<std::string, std::string>& b) {
            if (a.second != b.second) {
                return a.second < b.second;
            }
            return a.first < b.first;
        };
        std::ranges::sort(sorted_hash_ioi_string_pairs, comparator);
        for (auto & [hash, ioiString] : sorted_hash_ioi_string_pairs) {
            std::string listItemString;
            if (!ioiString.empty()) {
                listItemString = ioiString;
            } else {
                listItemString = hash;
            }
            SendMessage(hWndComboBox, (UINT) CB_ADDSTRING, (WPARAM) 0, reinterpret_cast<LPARAM>(listItemString.c_str()));
            if (selectedRpkgAirg.empty()) {
                selectedRpkgAirg = ioiString;
            }
        }
        SendMessage(hWndComboBox, CB_SETCURSEL, (WPARAM) 0, (LPARAM) 0);
    }
}

void Airg::extractAirgFromRpkgs(const std::string& hash) {
    if (!Rpkg::extractResourcesFromRpkgs({hash}, AIRG)) {
        const std::string fileName = NavKitSettings::getInstance().outputFolder + "\\airg\\" + hash + ".AIRG";
        Logger::log(NK_INFO, ("Loading airg from file: " + fileName).c_str());
        getInstance().loadAirgFromFile(fileName);
    }
}

INT_PTR CALLBACK Airg::extractAirgDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    Airg* pAirg = nullptr;
    if (message == WM_INITDIALOG) {
        pAirg = reinterpret_cast<Airg*>(lParam);
        SetWindowLongPtr(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(pAirg));
    } else {
        pAirg = reinterpret_cast<Airg*>(GetWindowLongPtr(hDlg, DWLP_USER));
    }

    if (!pAirg) {
        return (INT_PTR)FALSE;
    }

    switch (message) {
        case WM_INITDIALOG:
            updateAirgDialogControls(hDlg);
            return (INT_PTR) TRUE;

        case WM_COMMAND:

            if(HIWORD(wParam) == CBN_SELCHANGE) {
                const int ItemIndex = SendMessage((HWND) lParam, (UINT) CB_GETCURSEL,
                    (WPARAM) 0, (LPARAM) 0);
                char ListItem[256];
                SendMessage((HWND) lParam, (UINT) CB_GETLBTEXT, (WPARAM) ItemIndex, (LPARAM) ListItem);
                selectedRpkgAirg = ListItem;

                return TRUE;
            }
            if (const UINT commandId = LOWORD(wParam); commandId == IDC_BUTTON_LOAD_AIRG_FROM_RPKG) {
                for (auto & [hash, ioiString]: airgHashIoiStringMap) {
                    if (hash == selectedRpkgAirg) {
                        getInstance().loadedAirgText = hash;
                    } else if (ioiString == selectedRpkgAirg) {
                        getInstance().loadedAirgText = ioiString;
                    } else {
                        continue;
                    }
                    extractAirgFromRpkgs(hash);
                }
                DestroyWindow(hDlg);
                return TRUE;
            } else if (commandId == IDCANCEL) {
                DestroyWindow(hDlg);
                return TRUE;
            }
            return (INT_PTR) TRUE;

        case WM_CLOSE:
            DestroyWindow(hDlg);
            return (INT_PTR) TRUE;

        case WM_DESTROY:
            hAirgDialog = nullptr;
            return (INT_PTR) TRUE;
        default: ;
    }
    return (INT_PTR) FALSE;
}

void Airg::showExtractAirgDialog() {
    if (hAirgDialog) {
        SetForegroundWindow(hAirgDialog);
        return;
    }

    HINSTANCE hInstance = nullptr;
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&Airg::extractAirgDialogProc, &hInstance)) {
        Logger::log(NK_ERROR, "GetModuleHandleEx failed.");
        return;
    }

    HWND hParentWnd = Renderer::hwnd;

    hAirgDialog = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_EXTRACT_AIRG_DIALOG), hParentWnd, extractAirgDialogProc,
                                     reinterpret_cast<LPARAM>(this));

    if (hAirgDialog) {
        if (HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON))) {
            SendMessage(hAirgDialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessage(hAirgDialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }
        RECT parentRect, dialogRect;
        GetWindowRect(hParentWnd, &parentRect);
        GetWindowRect(hAirgDialog, &dialogRect);
        int parentWidth = parentRect.right - parentRect.left;
        int parentHeight = parentRect.bottom - parentRect.top;
        int dialogWidth = dialogRect.right - dialogRect.left;
        int dialogHeight = dialogRect.bottom - dialogRect.top;
        int newX = parentRect.left + (parentWidth - dialogWidth) / 2;
        int newY = parentRect.top + (parentHeight - dialogHeight) / 2;
        SetWindowPos(hAirgDialog, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        ShowWindow(hAirgDialog, SW_SHOW);
    } else {
        const DWORD error = GetLastError();
        Logger::log(NK_ERROR, "Failed to create dialog. Error code: %lu. Likely missing resource IDD_EXTRACT_AIRG_DIALOG in the DLL.", error);
    }
}
