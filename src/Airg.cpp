#include "..\include\NavKit\Airg.h"
#include "..\include\NavKit\FileUtil.h"

Airg::Airg(NavKit *navKit) : navKit(navKit) {
    airgName = "Load Airg";
    lastLoadAirgFile = airgName;
    saveAirgName = "Save Airg";
    lastSaveAirgFile = saveAirgName;
    airgLoaded = false;
    buildingVisionAndDeadEndData = false;
    connectWaypointModeEnabled = false;
    showAirg = true;
    showAirgIndices = false;
    showGrid = false;
    cellColorSource = 0.0f;
    airgScroll = 0;
    airgResourceConverter = HM3_GetConverterForResource("AIRG");;
    airgResourceGenerator = HM3_GetGeneratorForResource("AIRG");
    reasoningGrid = new ReasoningGrid();
    spacing = 2.25;
    zSpacing = 1;
    tolerance = 0.3;
    zTolerance = 1.0;
    doAirgHitTest = false;
    selectedWaypointIndex = -1;
}

Airg::~Airg() {
}

void Airg::resetDefaults() {
    spacing = 2.25;
    zSpacing = 1;
    tolerance = 0.3;
    zTolerance = 1.0;
}

void Airg::setLastLoadFileName(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        airgName = fileName;
        lastLoadAirgFile = airgName.data();
        airgLoaded = false;
        airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
    }
}

void Airg::setLastSaveFileName(const char *fileName) {
    saveAirgName = fileName;
    lastSaveAirgFile = saveAirgName.data();
    saveAirgName = saveAirgName.substr(saveAirgName.find_last_of("/\\") + 1);
}

void Airg::drawMenu() {
    if (imguiBeginScrollArea("Airg menu", navKit->renderer->width - 250 - 10, navKit->renderer->height - 10 - 390, 250,
                             390, &airgScroll))
        navKit->gui->mouseOverMenu = true;
    if (imguiCheck("Show Airg", showAirg))
        showAirg = !showAirg;
    if (imguiCheck("Show Airg Indices", showAirgIndices))
        showAirgIndices = !showAirgIndices;
    if (imguiCheck("Show Grid", showGrid))
        showGrid = !showGrid;
    imguiLabel("Cell color data source");
    imguiSlider("Off   Bitmap    Vision Data    Layer", &cellColorSource, 0.0f, 3.0f, 1.0f);

    imguiLabel("Load Airg from file");
    if (imguiButton(airgName.c_str(), (airgLoadState.empty() && airgSaveState.empty()))) {
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
                navKit->log(RC_LOG_PROGRESS, msg.data());
                std::thread loadAirgThread(&Airg::loadAirg, this, fileName, true);
                loadAirgThread.detach();
            } else if (extension == "AIRG") {
                delete reasoningGrid;
                reasoningGrid = new ReasoningGrid();
                std::string msg = "Loading Airg file: '";
                msg += fileName;
                msg += "'...";
                navKit->log(RC_LOG_PROGRESS, msg.data());
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
            navKit->log(RC_LOG_PROGRESS, msg.data());
            std::thread saveAirgThread(&Airg::saveAirg, this, fileName, extension == "JSON");
            saveAirgThread.detach();
        }
    }
    float lastSpacing = spacing;
    if (imguiSlider("Spacing", &spacing, 0.1f, 4.0f, 0.05f)) {
        if (lastSpacing != spacing) {
            saveSpacing(spacing);
            lastSpacing = spacing;
            navKit->log(RC_LOG_PROGRESS, ("Setting spacing to: " + std::to_string(spacing)).c_str());
        }
    }
    if (imguiButton("Reset Defaults")) {
        navKit->log(RC_LOG_PROGRESS, "Resetting Airg Default settings");
        resetDefaults();
    }
    if (imguiButton("Build Airg from Navp",
                    (navKit->navp->navpLoaded && airgLoadState.empty() && airgSaveState.empty()))) {
        airgLoaded = false;
        delete reasoningGrid;
        reasoningGrid = new ReasoningGrid();
        std::string msg = "Building Airg from Navp";
        navKit->log(RC_LOG_PROGRESS, msg.data());
        std::thread buildAirgThread(&ReasoningGrid::build, reasoningGrid, navKit->navp->navMesh, navKit, spacing,
                                    zSpacing, tolerance, zTolerance);
        buildAirgThread.detach();
    }
    if (imguiButton("Connect Waypoint", (airgLoaded && selectedWaypointIndex != -1 && !connectWaypointModeEnabled))) {
        connectWaypointModeEnabled = true;
        std::string msg = "Entering Connect Waypoint mode. Start waypoint: " + std::to_string(selectedWaypointIndex);
        navKit->log(RC_LOG_PROGRESS, msg.data());
    }
    imguiSeparatorLine();
    imguiLabel("Selected Waypoint");
    char selectedWaypointText[64];
    snprintf(selectedWaypointText, 64, selectedWaypointIndex != -1 ? "Waypoint Index: %d" : "Waypoint Index: None",
             selectedWaypointIndex);
    imguiValue(selectedWaypointText);

    imguiEndScrollArea();
}

void Airg::finalizeLoad() {
    if (airgLoadState.size() == 2) {
        airgLoadState.clear();
        airgLoaded = true;
    }
}

void Airg::finalizeSave() {
    if (airgSaveState.size() == 2) {
        airgSaveState.clear();
    }
}

void Airg::finalizeBuildVisionAndDeadEndData() {
    if (visionDataBuildState.size() == 2) {
        visionDataBuildState.clear();
    }
}

void Airg::saveTolerance(float newTolerance) {
    navKit->ini.SetValue("Airg", "tolerance", std::to_string(newTolerance).c_str());
    tolerance = newTolerance;
}

void Airg::saveSpacing(float newSpacing) {
    navKit->ini.SetValue("Airg", "spacing", std::to_string(newSpacing).c_str());
    spacing = newSpacing;
}

void Airg::saveZSpacing(float newZSpacing) {
    navKit->ini.SetValue("Airg", "ySpacing", std::to_string(newZSpacing).c_str());
    zSpacing = newZSpacing;
}

void Airg::saveZTolerance(float newZTolerance) {
    navKit->ini.SetValue("Airg", "yTolerance", std::to_string(newZTolerance).c_str());
    zTolerance = newZTolerance;
}

void Airg::connectWaypoints(int startWaypointIndex, int endWaypointIndex) {
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
    navKit->log(RC_LOG_PROGRESS,
                ("Connected waypoints: " + std::to_string(startWaypointIndex) + " and " + std::to_string(
                     endWaypointIndex)).c_str());
}

char *Airg::openAirgFileDialog(char *lastAirgFolder) {
    nfdu8filteritem_t filters[2] = {{"Airg files", "airg"}, {"Airg.json files", "airg.json"}};
    return FileUtil::openNfdLoadDialog(filters, 2, lastAirgFolder);
}

char *Airg::openSaveAirgFileDialog(char *lastAirgFolder) {
    nfdu8filteritem_t filters[2] = {{"Airg files", "airg"}, {"Airg.json files", "airg.json"}};
    return FileUtil::openNfdSaveDialog(filters, 2, "output", lastAirgFolder);
}

void renderWaypoint(const Waypoint &waypoint, bool fan) {
    if (fan) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex3f(waypoint.vPos.x, waypoint.vPos.z + 0.03, -waypoint.vPos.y);
    } else {
        glBegin(GL_LINE_LOOP);
    }

    const float r = 0.1f;
    for (int i = 0; i < 8; i++) {
        const float a = (float) i / 8.0f * RC_PI * 2;
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

void Airg::renderGrid(float gridSpacing, Vec3 color, float zOffset = 0.0) {
    float minX = reasoningGrid->m_Properties.vMin.x;
    float minY = reasoningGrid->m_Properties.vMin.y;
    int yi = -1, xi = -1;
    glColor4f(color.X, color.Y, color.Z, 0.6);
    float z = 0;
    //Vec3 camPos{ navKit->renderer->cameraPos[0], navKit->renderer->cameraPos[1], navKit->renderer->cameraPos[2] };
    for (float x = minX; x < reasoningGrid->m_Properties.vMax.x; x += gridSpacing) {
        xi++;
        yi = -1;
        for (float y = minY; y < reasoningGrid->m_Properties.vMax.y; y += gridSpacing) {
            yi++;
            //Vec3 pos{ x, y, z };
            //float distance = camPos.DistanceTo(pos);
            //if (distance > 100) {
            //	continue;
            //}
            glBegin(GL_LINE_LOOP);
            glVertex3f(x + gridSpacing, z + 0.02 - zOffset, -y);
            glVertex3f(x + gridSpacing, z + 0.02 - zOffset, -(y + gridSpacing));
            glVertex3f(x, z + 0.02 - zOffset, -(y + gridSpacing));
            glVertex3f(x, z + 0.02 - zOffset, -y);
            glEnd();
            Vec3 textCoords{x + gridSpacing / 2.0f, z + 0.02f - zOffset, -(y + gridSpacing / 2.0f)};
            int cellIndex = xi + yi * (reasoningGrid->m_Properties.nGridWidth);
            std::string cellText = std::to_string(xi) + ", " + std::to_string(yi) + " = " + std::to_string(cellIndex);
            navKit->renderer->drawText(cellText, textCoords, color);
        }
    }
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
    float gridZOffset = 0;
    if (selectedWaypointIndex != -1) {
        if (selectedWaypointIndex >= reasoningGrid->m_WaypointList.size()) {
            selectedWaypointIndex = -1;
        } else {
            const Waypoint &selectedWaypoint = reasoningGrid->m_WaypointList[selectedWaypointIndex];
            gridZOffset = -selectedWaypoint.vPos.z;
        }
    } else {
        gridZOffset = reasoningGrid->m_Properties.vMin.z;
    }

    int numWaypoints = reasoningGrid->m_WaypointList.size();
    for (size_t i = 0; i < numWaypoints; i++) {
        const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
        int size = visibilityDataSize(reasoningGrid, i);
        // cellColorSource options: 0 Off, 1 Bitmap, 2 Vision Data, 3 Layer
        if (cellColorSource == 1.0f) {
            renderCellBitmaps(i, i == selectedWaypointIndex);
        }
        if (cellColorSource == 2.0f && !buildingVisionAndDeadEndData) {
            renderVisionData(i, i == selectedWaypointIndex);
        }
        if (cellColorSource == 3.0f && !buildingVisionAndDeadEndData) {
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
    if (showAirgIndices) {
        for (size_t i = 0; i < numWaypoints; i++) {
            const Waypoint &waypoint = reasoningGrid->m_WaypointList[i];
            navKit->renderer->drawText(std::to_string(i), {waypoint.vPos.x, waypoint.vPos.z + 0.1f, -waypoint.vPos.y},
                                       {1, .7f, .7f});
        }
    }
    if (showGrid) {
        renderGrid(reasoningGrid->m_Properties.fGridSpacing, {0.6, 0.6, 0.6}, gridZOffset);
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
        navKit->log(RC_LOG_PROGRESS, ("Deselected waypoint: " + std::to_string(selectedWaypointIndex)).c_str());
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
        navKit->log(RC_LOG_PROGRESS, msg.c_str());
        navKit->log(RC_LOG_PROGRESS,
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

        std::string waypointVisibilityDataString;

        char numHex[3];
        msg += "  Visibility Data Type: " + std::to_string(waypointVisibilityData[0]); // +" Visibility data:";
        navKit->log(RC_LOG_PROGRESS, msg.c_str());
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
                //navKit->log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
                waypointVisibilityDataString = "";
            }
        }
        //navKit->log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
        const unsigned int colorRgb = (waypoint.nLayerIndex << 6) | 0xC0000000;
        std::string hexColor = std::format("{:x}", colorRgb);
        navKit->log(RC_LOG_PROGRESS, ("Layer Index RGB " + hexColor).c_str());
    }
}

void Airg::saveAirg(Airg *airg, std::string fileName, bool isJson) {
    airg->airgSaveState.push_back(true);
    std::string s_OutputFileName = std::filesystem::path(fileName).string();
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Airg to file at ";
    msg += std::ctime(&start_time);
    airg->navKit->log(RC_LOG_PROGRESS, msg.data());
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
    airg->navKit->log(RC_LOG_PROGRESS, msg.data());
    airg->airgSaveState.push_back(true);
}

void Airg::loadAirg(Airg *airg, char *fileName, bool isFromJson) {
    airg->airgLoadState.push_back(true);
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Airg from file at ";
    msg += std::ctime(&start_time);
    airg->navKit->log(RC_LOG_PROGRESS, msg.data());
    auto start = std::chrono::high_resolution_clock::now();

    std::string jsonFileName = fileName;
    if (!isFromJson) {
        std::string nameWithoutExtension = jsonFileName.substr(0, jsonFileName.length() - 5);

        jsonFileName = nameWithoutExtension + ".temp.airg.json";
        airg->airgResourceConverter->FromResourceFileToJsonFile(fileName, jsonFileName.data());
    }
    airg->reasoningGrid->readJson(jsonFileName.data());
    airg->saveSpacing(airg->reasoningGrid->m_Properties.fGridSpacing);
    if (!isFromJson) {
        std::filesystem::remove(jsonFileName);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    msg = "Finished loading Airg in ";
    msg += std::to_string(duration.count());
    msg += " seconds";
    airg->navKit->log(RC_LOG_PROGRESS, msg.data());
    airg->navKit->log(RC_LOG_PROGRESS,
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
            airg->navKit->log(RC_LOG_PROGRESS,
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
    airg->navKit->log(RC_LOG_PROGRESS,
                      ("Vision Data Offset[" + std::to_string(airg->reasoningGrid->m_WaypointList.size() - 1) + "]: " +
                       std::to_string(
                           airg->reasoningGrid->m_WaypointList[airg->reasoningGrid->m_WaypointList.size() - 1].
                           nVisionDataOffset) + " Max Visibility: " + std::to_string(
                           airg->reasoningGrid->m_pVisibilityData.size()) + " Difference : " + std::to_string(
                           finalVisionDataSize)).c_str());
    total += finalVisionDataSize;
    airg->navKit->log(RC_LOG_PROGRESS,
                      ("Total: " + std::to_string(total) + " Max Visibility: " + std::to_string(
                           airg->reasoningGrid->m_pVisibilityData.size())).c_str());
    airg->navKit->log(RC_LOG_PROGRESS, "Visibility data offset map:");
    for (auto &pair: visionDataOffsetCounts) {
        VisionData visionData = VisionData::GetVisionDataType(pair.first);
        airg->navKit->log(RC_LOG_PROGRESS,
                          ("Offset difference: " + std::to_string(pair.first) + " Color: " + visionData.getName() +
                           " Count: " + std::to_string(pair.second)).c_str());
    }

    airg->airgLoadState.push_back(true);
}
