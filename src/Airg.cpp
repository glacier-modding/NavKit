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

void Airg::drawMenu() {
	if (imguiBeginScrollArea("Airg menu", navKit->width - 250 - 10, navKit->height - 10 - 200 - 10, 250, 200, &airgScroll))
		navKit->mouseOverMenu = true;
	if (imguiCheck("Show Airg", showAirg))
		showAirg = !showAirg;
	imguiLabel("Load Airg from file");
	if (imguiButton(airgName.c_str(), airgLoadDone.empty())) {
		char* fileName = openAirgFileDialog(lastLoadAirgFile.data());
		if (fileName)
		{
			airgName = fileName;
			lastLoadAirgFile = airgName.data();
			airgLoaded = false;
			airgName = airgName.substr(airgName.find_last_of("/\\") + 1);
			std::string extension = airgName.substr(airgName.length() - 4, airgName.length());
			std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

			if (extension == "JSON") {
				delete reasoningGrid;
				reasoningGrid = new ReasoningGrid();
				std::string msg = "Loading Airg.Json file: '";
				msg += fileName;
				msg += "'...";
				navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
				std::thread loadAirgThread(&Airg::loadAirg, this, lastLoadAirgFile.data(), true);
				loadAirgThread.detach();
			}
			else if (extension == "AIRG") {
				delete reasoningGrid;
				reasoningGrid = new ReasoningGrid();
				std::string msg = "Loading Airg file: '";
				msg += fileName;
				msg += "'...";
				navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
				std::thread loadAirgThread(&Airg::loadAirg, this, lastLoadAirgFile.data(), false);
				loadAirgThread.detach();
			}
		}
	}
	imguiLabel("Save Airg to file");
	if (imguiButton(saveAirgName.c_str(), airgLoaded)) {
		char* fileName = openSaveAirgFileDialog(lastLoadAirgFile.data());
		if (fileName)
		{
			saveAirgName = fileName;
			lastSaveAirgFile = saveAirgName.data();
			saveAirgName = saveAirgName.substr(saveAirgName.find_last_of("/\\") + 1);
			std::string extension = saveAirgName.substr(saveAirgName.length() - 4, saveAirgName.length());
			std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
			std::string msg = "Saving Airg";
			if (extension == "JSON") {
				msg += ".Json";
			}
			msg += " file: '";
			msg += fileName;
			msg += "'...";
			navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
			if (extension == "JSON") {
				saveAirg(lastSaveAirgFile.data());
			}
			else if (extension == "AIRG") {
				std::string tempJsonFile = lastSaveAirgFile.data();
				tempJsonFile += ".temp.json";
				saveAirg(tempJsonFile.data());
				airgResourceGenerator->FromJsonFileToResourceFile(tempJsonFile.data(), lastSaveAirgFile.data(), false);
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
		navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
		reasoningGrid->build(navKit->navp->navMesh, &navKit->ctx);
		airgLoaded = true;
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
	return FileUtil::openNfdLoadDialog(filters, 2);
}

char* Airg::openSaveAirgFileDialog(char* lastAirgFolder) {
	nfdu8filteritem_t filters[2] = { { "Airg files", "airg" }, { "Airg.json files", "airg.json" } };
	return FileUtil::openNfdSaveDialog(filters, 2, "output");
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

void Airg::saveAirg(char* fileName) {
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
	airg->navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();

	std::string jsonFileName = fileName;
	if (!isFromJson) {
		jsonFileName += ".Json";
		airg->airgResourceConverter->FromResourceFileToJsonFile(fileName, jsonFileName.data());
	}
	airg->reasoningGrid->readJson(jsonFileName.data());
	if (airg->airgLoadDone.empty()) {
		airg->airgLoadDone.push_back(true);
	}
	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished loading Airg in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	airg->navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
}