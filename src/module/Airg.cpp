#include "../../include/NavKit/module/Airg.h"

#include <filesystem>
#include <fstream>
#include <numbers>

#include <CommCtrl.h>
#include <iomanip>
#include <SDL.h>
#include <sstream>
#include <string>
#include <GL/glew.h>
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/model/ReasoningGrid.h"
#include "../../include/NavKit/model/VisionData.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/GridGenerator.h"
#include "../../include/ResourceLib_HM3/ResourceConverter.h"
#include "../../include/ResourceLib_HM3/ResourceGenerator.h"
#include "../../include/ResourceLib_HM3/ResourceLib_HM3.h"
#include "../../include/ResourceLib_HM3/Generated/HM3/ZHMGen.h"

HWND Airg::hAirgDialog = nullptr;

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
            }
        }
        else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_XOFFSET)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            grid.xOffset = -grid.spacing + (pos * 0.05f);
            SetDlgItemText(hDlg, IDC_STATIC_XOFFSET_VAL, format_float(grid.xOffset).c_str());
        }
        else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_ZOFFSET)) {
            int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
            grid.yOffset = -grid.spacing + (pos * 0.05f);
            SetDlgItemText(hDlg, IDC_STATIC_ZOFFSET_VAL, format_float(grid.yOffset).c_str());
        }
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == IDC_BUTTON_RESET_DEFAULTS) {
            Logger::log(NK_INFO, "Resetting Airg Default settings");
            resetDefaults();
            UpdateDialogControls(hDlg);
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

void Airg::handleOpenAirgClicked() {
    if (const char *fileName = openAirgFileDialog(lastLoadAirgFile.data())) {
        setLastLoadFileName(fileName);
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
    airgLoaded = false;
    Menu::updateMenuState();
    airgBuilding = true;
    delete reasoningGrid;
    reasoningGrid = new ReasoningGrid();
    std::string msg = "Building Airg from Navp";
    Logger::log(NK_INFO, msg.data());
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
    backgroundWorker.emplace(&GridGenerator::build, &GridGenerator::getInstance());
}

// Render Layer Index
void Airg::renderLayerIndices(int waypointIndex) const {
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

void Airg::renderVisionData(int waypointIndex, bool selected) const {
    const float size = visibilityDataSize(reasoningGrid, waypointIndex);
    const Waypoint &waypoint = reasoningGrid->m_WaypointList[waypointIndex];
    const float x = waypoint.vPos.x;
    const float y = waypoint.vPos.y;
    const std::vector<uint8_t> waypointVisionData = reasoningGrid->getWaypointVisionData(waypointIndex);
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
            renderLayerIndices(i);
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

void Airg::renderAirgForHitTest() const {
    if (showAirg && airgLoaded) {
        const int numWaypoints = reasoningGrid->m_WaypointList.size();
        for (size_t i = 0; i < numWaypoints; i++) {
            const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
            glColor3ub(AIRG_WAYPOINT, i / 255, i % 255);
            renderWaypoint(waypoint, true);
        }
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
    Menu::updateMenuState();
}
