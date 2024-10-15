#include "..\include\NavKit\Airg.h"
#include "..\include\NavKit\FileUtil.h"

Airg::Airg(NavKit* navKit) : navKit(navKit) {
	airgName = "Load Airg";
	lastLoadAirgFile = airgName;
	saveAirgName = "Save Airg";
	lastSaveAirgFile = saveAirgName;
	airgLoaded = false;
	showAirg = true;
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
}

void Airg::setLastLoadFileName(const char* fileName) {
	if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
		airgName = fileName;
		lastLoadAirgFile = airgName.data();
		airgLoaded = false;
		airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
	}
}

void Airg::setLastSaveFileName(const char* fileName) {
	saveAirgName = fileName;
	lastSaveAirgFile = saveAirgName.data();
	saveAirgName = saveAirgName.substr(saveAirgName.find_last_of("/\\") + 1);
}

void Airg::drawMenu() {
	if (imguiBeginScrollArea("Airg menu", navKit->renderer->width - 250 - 10, navKit->renderer->height - 10 - 347, 250, 347, &airgScroll))
		navKit->gui->mouseOverMenu = true;
	if (imguiCheck("Show Airg", showAirg))
		showAirg = !showAirg;
	imguiLabel("Load Airg from file");
	if (imguiButton(airgName.c_str(), (airgLoadState.empty() && airgSaveState.empty()))) {
		char* fileName = openAirgFileDialog(lastLoadAirgFile.data());
		if (fileName)
		{
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
			}
			else if (extension == "AIRG") {
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
		char* fileName = openSaveAirgFileDialog(lastLoadAirgFile.data());
		if (fileName)
		{
			setLastSaveFileName(fileName);
			std::string fileNameString = std::string{ fileName };
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
	float lastTolerance = tolerance;
	if (imguiSlider("Tolerance", &tolerance, 0.0f, 4.0f, 0.05f)) {
		if (lastTolerance != tolerance) {
			saveTolerance(tolerance);
			lastTolerance = tolerance;
			navKit->log(RC_LOG_PROGRESS, ("Setting tolerance to: " + std::to_string(tolerance)).c_str());
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
	float lastZSpacing = zSpacing;
	if (imguiSlider("Y Spacing", &zSpacing, 0.1f, 4.0f, 0.05f)) {
		if (lastZSpacing != zSpacing) {
			saveZSpacing(zSpacing);
			lastZSpacing = zSpacing;
			navKit->log(RC_LOG_PROGRESS, ("Setting Y spacing to: " + std::to_string(zSpacing)).c_str());
		}
	}
	float lastZTolerance= zTolerance;
	if (imguiSlider("Y Tolerance", &zTolerance, 0.1f, 4.0f, 0.05f)) {
		if (lastZTolerance != zTolerance) {
			saveZTolerance(zTolerance);
			lastZTolerance = zTolerance;
			navKit->log(RC_LOG_PROGRESS, ("Setting Y tolerance to: " + std::to_string(zTolerance)).c_str());
		}
	}
	if (imguiButton("Reset Defaults")) {
		navKit->log(RC_LOG_PROGRESS, "Resetting Airg Default settings");
		resetDefaults();
	}
	if (imguiButton("Build Airg from Navp", (navKit->navp->navpLoaded && airgLoadState.empty() && airgSaveState.empty()))) {
		airgLoaded = false;
		delete reasoningGrid;
		reasoningGrid = new ReasoningGrid();
		std::string msg = "Building Airg from Navp";
		navKit->log(RC_LOG_PROGRESS, msg.data());
		std::thread buildAirgThread(&ReasoningGrid::build, reasoningGrid, navKit->navp->navMesh, navKit, spacing, zSpacing, tolerance, zTolerance);
		buildAirgThread.detach();
	}
	imguiSeparatorLine();
	imguiLabel("Selected Waypoint");
	char selectedWaypointText[64];
	snprintf(selectedWaypointText, 64, selectedWaypointIndex != -1 ? "Waypoint Index: %d" : "Waypoint Index: None", selectedWaypointIndex);
	imguiValue(selectedWaypointText);

	imguiEndScrollArea();
}

void Airg::finalizeLoad() {
	if (airgLoadState.size() == 2) {
		airgLoadState.clear();
	}
}

void Airg::finalizeSave() {
	if (airgSaveState.size() == 2) {
		airgSaveState.clear();
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

char* Airg::openAirgFileDialog(char* lastAirgFolder) {
	nfdu8filteritem_t filters[2] = { { "Airg files", "airg" }, { "Airg.json files", "airg.json" } };
	return FileUtil::openNfdLoadDialog(filters, 2, lastAirgFolder);
}

char* Airg::openSaveAirgFileDialog(char* lastAirgFolder) {
	nfdu8filteritem_t filters[2] = { { "Airg files", "airg" }, { "Airg.json files", "airg.json" } };
	return FileUtil::openNfdSaveDialog(filters, 2, "output", lastAirgFolder);
}

void renderWaypoint(const Waypoint& waypoint, bool fan) {
	if (fan) {
		glBegin(GL_TRIANGLE_FAN);
		glVertex3f(waypoint.vPos.x, waypoint.vPos.z, -waypoint.vPos.y);
	}
	else {
		glBegin(GL_LINE_LOOP);
	}

	const float r = 0.1f;
	for (int i = 0; i < 8; i++) {
		const float a = (float)i / 8.0f * RC_PI * 2;
		const float fx = (float)waypoint.vPos.x + cosf(a) * r;
		const float fy = (float)waypoint.vPos.y + sinf(a) * r;
		const float fz = (float)waypoint.vPos.z;
		glVertex3f(fx, fz, -fy);
	}
	if (fan) {
		const float fx = (float)waypoint.vPos.x + cosf(0) * r;
		const float fy = (float)waypoint.vPos.y + sinf(0) * r;
		const float fz = (float)waypoint.vPos.z;
		glVertex3f(fx, fz, -fy);
	}
	glEnd();
}

int visibilityDataSize(ReasoningGrid* reasoningGrid, int waypointIndex) {
	int offset1 = reasoningGrid->m_WaypointList[waypointIndex].nVisionDataOffset;
	int offset2;
	if (waypointIndex < (reasoningGrid->m_WaypointList.size() - 1)) {
		offset2 = reasoningGrid->m_WaypointList[waypointIndex + 1].nVisionDataOffset;
	}
	else {
		offset2 = reasoningGrid->m_pVisibilityData.size();
	}
	return offset2 - offset1;
}

void Airg::renderAirg() {
	int numWaypoints = reasoningGrid->m_WaypointList.size();
	for (size_t i = 0; i < numWaypoints; i++) {
		const Waypoint& waypoint = reasoningGrid->m_WaypointList[i];
		int size = visibilityDataSize(reasoningGrid, i);
		switch (size) {
			case 556:
				glColor4f(1.0, 0.0, 0.0, 0.6);
				break;
			case 1110:
				glColor4f(0.0, 1.0, 0.0, 0.6);
				break;
			case 1664:
				glColor4f(0.0, 0.0, 1.0, 0.6);
				break;
			case 2218:
				glColor4f(1.0, 1.0, 0.0, 0.6);
				break;
			case 2772:
				glColor4f(1.0, 0.0, 1.0, 0.6);
				break;
			case 3326:
				glColor4f(0.0, 1.0, 1.0, 0.6);
				break;
			case 3880:
				glColor4f(1.0, 1.0, 1.0, 0.6);
				break;
			default:
				glColor4f(0.0, 0.0, 0.0, 0.6);
				break;
		}
		renderWaypoint(waypoint, selectedWaypointIndex == i);

		for (int neighborIndex = 0; neighborIndex < waypoint.nNeighbors.size(); neighborIndex++) {
			if (waypoint.nNeighbors[neighborIndex] != 65535) {
				const Waypoint& neighbor = reasoningGrid->m_WaypointList[waypoint.nNeighbors[neighborIndex]];
				glBegin(GL_LINES);
				glVertex3f(waypoint.vPos.x, waypoint.vPos.z, -waypoint.vPos.y);
				glVertex3f(neighbor.vPos.x, neighbor.vPos.z, -neighbor.vPos.y);
				glEnd();
			}
		}
	}
}

void Airg::renderAirgForHitTest() {
	int numWaypoints = reasoningGrid->m_WaypointList.size();
	for (size_t i = 0; i < numWaypoints; i++) {
		const Waypoint& waypoint = reasoningGrid->m_WaypointList[i];
		glColor3ub(61, i / 255, i % 255);
		renderWaypoint(waypoint, true);
	}
}

void Airg::setSelectedAirgWaypointIndex(int index) {
	if (index == -1 && selectedWaypointIndex != -1) {
		navKit->log(RC_LOG_PROGRESS, ("Deselected waypoint: " + std::to_string(selectedWaypointIndex)).c_str());
	}
	selectedWaypointIndex = index;
	if (index != -1 && index < reasoningGrid->m_WaypointList.size()) {
		Waypoint waypoint = reasoningGrid->m_WaypointList[selectedWaypointIndex];
		std::string msg = "Airg Waypoint Index: " + std::to_string(index);
		for (int i = 0; i < 8; i++) {
			int neighborIndex = waypoint.nNeighbors[i];
			if (neighborIndex != 65535) {
				msg += "  Neighbor " + std::to_string(i) + " Waypoint Index : " + std::to_string(neighborIndex);
			}
		}
		navKit->log(RC_LOG_PROGRESS, msg.c_str());
		navKit->log(RC_LOG_PROGRESS, ("Waypoint position: X: " + std::to_string(waypoint.vPos.x) + "Y: " + std::to_string(waypoint.vPos.y) + "Z: " + std::to_string(waypoint.vPos.z)).c_str());
		msg = "  Vision Data Offset: " + std::to_string(waypoint.nVisionDataOffset);
		msg += "  Layer Index: " + std::to_string(waypoint.nLayerIndex);
		int nextWaypointOffset = (index + 1) < reasoningGrid->m_WaypointList.size() ? reasoningGrid->m_WaypointList[index + 1].nVisionDataOffset : reasoningGrid->m_pVisibilityData.size();
		int visibilityDataSize = nextWaypointOffset - waypoint.nVisionDataOffset;
		msg += "  Visibility Data size: " + std::to_string(visibilityDataSize);

		std::vector<uint8_t>::const_iterator first = reasoningGrid->m_pVisibilityData.begin() + waypoint.nVisionDataOffset;
		std::vector<uint8_t>::const_iterator last = (index + 1) < reasoningGrid->m_WaypointList.size() ?
			reasoningGrid->m_pVisibilityData.begin() + reasoningGrid->m_WaypointList[index + 1].nVisionDataOffset :
			reasoningGrid->m_pVisibilityData.end();
		std::vector<uint8_t> waypointVisibilityData(first, last);

		std::string waypointVisibilityDataString;

		char numHex[3];
		msg += "  Visibility Data Type: " + std::to_string(waypointVisibilityData[0]) + " Visibility data:";
		navKit->log(RC_LOG_PROGRESS, msg.c_str());
		for (int count = 2; count < waypointVisibilityData.size(); count++) {
			uint8_t num = waypointVisibilityData[count];
			sprintf(numHex, "%02X", num);
			if (numHex[0] == '0' && numHex[1] == '0') {
				numHex[0] = '~';
				numHex[1] = '~';
			}
			waypointVisibilityDataString += std::string{ numHex };
			waypointVisibilityDataString += " ";
			if ((count - 1) % 8 == 0) {
				waypointVisibilityDataString += " ";
			}
			if ((count - 1) % 96 == 0) {
				navKit->log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
				waypointVisibilityDataString = "";
			}
		}
		navKit->log(RC_LOG_PROGRESS, ("  " + waypointVisibilityDataString).c_str());
	}
}

void Airg::saveAirg(Airg* airg, std::string fileName, bool isJson) {
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
		airg->airgResourceGenerator->FromJsonFileToResourceFile(tempJsonFile.data(), std::string{ fileName }.c_str(), false);
		std::filesystem::remove(tempJsonFile);
	}
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished saving Airg to " + std::string{ fileName } + " in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	airg->navKit->log(RC_LOG_PROGRESS, msg.data());
	airg->airgSaveState.push_back(true);
}

void Airg::loadAirg(Airg* airg, char* fileName, bool isFromJson) {
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
	airg->navKit->log(RC_LOG_PROGRESS, ("Waypoint count: " + std::to_string(airg->reasoningGrid->m_WaypointList.size()) + ", Visibility Data size: " + std::to_string(airg->reasoningGrid->m_pVisibilityData.size()) + ", Visibility Data points per waypoint: " + std::to_string(static_cast<double>(airg->reasoningGrid->m_pVisibilityData.size()) / static_cast<double>(airg->reasoningGrid->m_WaypointList.size()))).c_str());
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
			airg->navKit->log(RC_LOG_PROGRESS, ("Vision Data Offset[" + std::to_string(i - 1) + "]: " + std::to_string(w1.nVisionDataOffset) + " Vision Data Offset[" + std::to_string(i) + "]: " + std::to_string(w2.nVisionDataOffset) + " Difference : " + std::to_string(lastVisionDataSize) + " Count: " + std::to_string(count)).c_str());
			if (visionDataOffsetCounts.count(visionDataSize) > 0) {
				int cur = visionDataOffsetCounts[visionDataSize];
				visionDataOffsetCounts[visionDataSize] = cur + 1;
			}
			else {
				visionDataOffsetCounts[visionDataSize] = 1;
			}
			count = 1;
		}
		else {
			count++;
		}
	}
	int finalVisionDataSize = airg->reasoningGrid->m_pVisibilityData.size() - airg->reasoningGrid->m_WaypointList[airg->reasoningGrid->m_WaypointList.size() - 1].nVisionDataOffset;
	airg->navKit->log(RC_LOG_PROGRESS, ("Vision Data Offset[" + std::to_string(airg->reasoningGrid->m_WaypointList.size() - 1) + "]: " + std::to_string(airg->reasoningGrid->m_WaypointList[airg->reasoningGrid->m_WaypointList.size() - 1].nVisionDataOffset) + " Max Visibility: " + std::to_string(airg->reasoningGrid->m_pVisibilityData.size()) + " Difference : " + std::to_string(finalVisionDataSize)).c_str());
	total += finalVisionDataSize;
	airg->navKit->log(RC_LOG_PROGRESS, ("Total: " + std::to_string(total) + " Max Visibility: " + std::to_string(airg->reasoningGrid->m_pVisibilityData.size())).c_str());
	airg->navKit->log(RC_LOG_PROGRESS, "Visibility data offset map:");
	for (auto& pair : visionDataOffsetCounts) {
		std::string color;
		switch (pair.first) {
		case 556:
			color = "red";
			break;
		case 1110:
			color = "green";
			break;
		case 1664:
			color = "blue";
			break;
		case 2218:
			color = "yellow";
			break;
		case 2772:
			color = "purple";
			break;
		case 3326:
			color = "teal";
			break;
		case 3880:
			color = "white";
			break;
		default:
			color = "black";
			break;
		}
		airg->navKit->log(RC_LOG_PROGRESS, ("Offset difference: " + std::to_string(pair.first) + " Color: " + color + " Count: " + std::to_string(pair.second)).c_str());
	}

	airg->airgLoadState.push_back(true);
}