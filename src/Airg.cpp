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
}

Airg::~Airg() {
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
	if (imguiBeginScrollArea("Airg menu", navKit->renderer->width - 250 - 10, navKit->renderer->height - 10 - 200 - 10, 250, 200, &airgScroll))
		navKit->gui->mouseOverMenu = true;
	if (imguiCheck("Show Airg", showAirg))
		showAirg = !showAirg;
	imguiLabel("Load Airg from file");
	if (imguiButton(airgName.c_str(), airgLoadDone.empty())) {
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
	if (imguiButton(saveAirgName.c_str(), airgLoaded)) {
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
			if (extension == "JSON") {
				saveAirg(fileName);
				navKit->log(RC_LOG_PROGRESS, ("Finished saving Airg to " + std::string{ fileName } + ".").c_str());
			}
			else if (extension == "AIRG") {
				std::string tempJsonFile = fileName;
				tempJsonFile += ".temp.json";
				saveAirg(tempJsonFile);
				airgResourceGenerator->FromJsonFileToResourceFile(tempJsonFile.data(), fileName, false);
				navKit->log(RC_LOG_PROGRESS, ("Finished saving Airg to " + std::string{ fileName } + ".").c_str());
				std::filesystem::remove(tempJsonFile);
			}
		}
	}
	imguiLabel("Build Airg from Navp");
	if (imguiButton("Build Airg", navKit->navp->navpLoaded)) {
		airgLoaded = false;
		delete reasoningGrid;
		reasoningGrid = new ReasoningGrid();
		std::string msg = "Building Airg from Navp";
		navKit->log(RC_LOG_PROGRESS, msg.data());
		std::thread buildAirgThread(&ReasoningGrid::build, reasoningGrid, navKit->navp->navMesh, navKit);
		buildAirgThread.detach();
	}
	imguiEndScrollArea();
}

void Airg::finalizeLoad() {
	if (airgLoadDone.size() == 1) {
		airgLoadDone.clear();
		airgLoaded = true;
	}
}

char* Airg::openAirgFileDialog(char* lastAirgFolder) {
	nfdu8filteritem_t filters[2] = { { "Airg files", "airg" }, { "Airg.json files", "airg.json" } };
	return FileUtil::openNfdLoadDialog(filters, 2, lastAirgFolder);
}

char* Airg::openSaveAirgFileDialog(char* lastAirgFolder) {
	nfdu8filteritem_t filters[2] = { { "Airg files", "airg" }, { "Airg.json files", "airg.json" } };
	return FileUtil::openNfdSaveDialog(filters, 2, "output", lastAirgFolder);
}

void Airg::renderAirg() {
	int numWaypoints = reasoningGrid->m_WaypointList.size();
	for (size_t i = 0; i < numWaypoints; i++) {
		const Waypoint& waypoint = reasoningGrid->m_WaypointList[i];
		glColor4f(1.0, 0.0, 0, 0.6);
		glBegin(GL_LINE_LOOP);
		const float r = 0.1f;
		for (int i = 0; i < 8; i++) {
			const float a = (float)i / 8.0f * RC_PI * 2;
			const float fx = (float)waypoint.vPos.x + cosf(a) * r;
			const float fy = (float)waypoint.vPos.y + sinf(a) * r;
			const float fz = (float)waypoint.vPos.z;
			glVertex3f(fx, fz, -fy);
		}
		glEnd();
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

void Airg::saveAirg(std::string fileName) {
	const std::string s_OutputFileName = std::filesystem::path(fileName).string();
	std::filesystem::remove(s_OutputFileName);

	// Write the airg to JSON file
	std::ofstream fileOutputStream(s_OutputFileName);
	reasoningGrid->writeJson(fileOutputStream);
	fileOutputStream.close();
}

void Airg::loadAirg(Airg* airg, char* fileName, bool isFromJson) {
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
	if (airg->airgLoadDone.empty()) {
		airg->airgLoadDone.push_back(true);
	}
	if (!isFromJson) {
		std::filesystem::remove(jsonFileName);
	}
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished loading Airg in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	airg->navKit->log(RC_LOG_PROGRESS, msg.data());
}