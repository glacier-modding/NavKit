#include "..\include\NavKit\Navp.h"

Navp::Navp(NavKit* navKit): navKit(navKit) {
	loadNavpName = "Load Navp";
	lastLoadNavpFile = loadNavpName;
	saveNavpName = "Save Navp";
	lastSaveNavpFile = saveNavpName;
	navpLoaded = false;
	showNavp = true;
	doNavpHitTest = false;
	navMesh = new NavPower::NavMesh();
	selectedNavpArea = -1;
	navpScroll = 0;
}

Navp::~Navp() {
}

char* Navp::openLoadNavpFileDialog(char* lastNavpFolder) {
	nfdu8filteritem_t filters[2] = { { "Navp files", "navp" }, { "Navp.json files", "navp.json" } };
	return FileUtil::openNfdLoadDialog(filters, 2);
}

char* Navp::openSaveNavpFileDialog(char* lastNavpFolder) {
	nfdu8filteritem_t filters[2] = { { "Navp files", "navp" }, { "Navp.json files", "navp.json" } };
	return FileUtil::openNfdSaveDialog(filters, 2, "output");
}

void Navp::renderArea(NavPower::Area area, bool selected) {
	if (!selected) {
		glColor4f(0.0, 0.0, 0.5, 0.6);
	}
	else {
		glColor4f(0.0, 0.5, 0.5, 0.6);
	}
	glBegin(GL_POLYGON);
	for (auto vertex : area.m_edges) {
		glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
	}
	glEnd();
	if (!selected) {
		glColor3f(0.0, 0.0, 1.0);
	}
	else {
		glColor3f(0.0, 1.0, 1.0);
	}
	glBegin(GL_LINES);
	for (auto vertex : area.m_edges) {
		glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
	}
	glEnd();
}

void Navp::renderNavMesh() {
	int areaIndex = 0;
	for (const NavPower::Area& area : navMesh->m_areas) {
		renderArea(area, areaIndex == selectedNavpArea);
		areaIndex++;
	}
}

void Navp::renderNavMeshForHitTest() {
	int areaIndex = 0;
	for (const NavPower::Area& area : navMesh->m_areas) {
		glColor3ub(60, areaIndex / 255, areaIndex % 255);
		areaIndex++;
		glBegin(GL_POLYGON);
		for (auto vertex : area.m_edges) {
			glVertex3f(vertex->m_pos.X, vertex->m_pos.Z, -vertex->m_pos.Y);
		}
		glEnd();
	}
}

void Navp::loadNavMesh(Navp* navp, char* fileName, bool isFromJson) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string msg = "Loading Navp from file at ";
	msg += std::ctime(&start_time);
	navp->navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();
	try {
		NavPower::NavMesh newNavMesh = isFromJson ? LoadNavMeshFromJson(fileName) : LoadNavMeshFromBinary(fileName);
		std::swap(*navp->navMesh, newNavMesh);
	}
	catch (...) {
		msg = "Invalid Navp file: '";
		msg += fileName;
		msg += "'...";
		navp->navKit->ctx.log(RC_LOG_ERROR, msg.data());
		return;
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished loading Navp in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	navp->navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
}

void Navp::buildNavp(Navp* navp) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string msg = "Building Navp at ";
	msg += std::ctime(&start_time);
	navp->navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();
	if (navp->navKit->sample->handleBuild()) {
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
		navp->navpBuildDone.push_back(true);
		msg = "Finished building Navp in ";
		msg += std::to_string(duration.count());
		msg += " seconds";
		navp->navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
	}
	else {
		navp->navKit->ctx.log(RC_LOG_ERROR, "Error building Navp");
	}
}
	
void Navp::drawMenu() {
	if (imguiBeginScrollArea("Navp menu", 10, navKit->height - 225 - 935 - 15, 250, 935, &navpScroll))
		navKit->mouseOverMenu = true;
	if (imguiCheck("Show Navp", showNavp))
		showNavp = !showNavp;
	imguiLabel("Load Navp from file");
	if (imguiButton(loadNavpName.c_str(), navpLoadDone.empty())) {
		char* fileName = openLoadNavpFileDialog(lastLoadNavpFile.data());
		if (fileName)
		{
			loadNavpName = fileName;
			lastLoadNavpFile = loadNavpName.data();
			loadNavpName = loadNavpName.substr(loadNavpName.find_last_of("/\\") + 1);
			std::string extension = loadNavpName.substr(loadNavpName.length() - 4, loadNavpName.length());
			std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);

			if (extension == "JSON") {
				navpLoaded = true;
				std::string msg = "Loading Navp.json file: '";
				msg += fileName;
				msg += "'...";
				navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
				std::thread loadNavMeshThread(&Navp::loadNavMesh, this, lastLoadNavpFile.data(), true);
				loadNavMeshThread.detach();
			}
			else if (extension == "NAVP") {
				navpLoaded = true;
				std::string msg = "Loading Navp file: '";
				msg += fileName;
				msg += "'...";
				navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
				std::thread loadNavMeshThread(&Navp::loadNavMesh, this, lastLoadNavpFile.data(), false);
				loadNavMeshThread.detach();
			}
		}
	}
	imguiLabel("Save Navp to file");
	if (imguiButton(saveNavpName.c_str(), navpLoaded)) {
		char* fileName = openSaveNavpFileDialog(lastLoadNavpFile.data());
		if (fileName)
		{
			saveNavpName = fileName;
			lastSaveNavpFile = saveNavpName.data();
			saveNavpName = saveNavpName.substr(saveNavpName.find_last_of("/\\") + 1);
			std::string extension = saveNavpName.substr(saveNavpName.length() - 4, saveNavpName.length());
			std::transform(extension.begin(), extension.end(), extension.begin(), ::toupper);
			std::string msg = "Saving Navp";
			if (extension == "JSON") {
				msg += ".Json";
			}
			msg += " file: '";
			msg += fileName;
			msg += "'...";
			navKit->ctx.log(RC_LOG_PROGRESS, msg.data());
			if (extension == "JSON") {
				OutputNavMesh_JSON_Write(navMesh, lastSaveNavpFile.data());
			}
			else if (extension == "NAVP") {
				OutputNavMesh_NAVP_Write(navMesh, lastSaveNavpFile.data());
			}
		}
	}
	imguiLabel("Send Navp to game");
	if (imguiButton("Send NAVP", navpLoaded)) {
		navKit->gameConnection = new GameConnection(&navKit->ctx);
		navKit->gameConnection->SendNavp(navMesh);
		navKit->gameConnection->HandleMessages();
	}
	char selectedNavpText[64];
	snprintf(selectedNavpText, 64, selectedNavpArea != -1 ? "Selected Area: %d" : "Selected Area: None", selectedNavpArea);
	imguiValue(selectedNavpText);
	imguiSeparatorLine();

	navKit->sample->handleCommonSettings();

	if (imguiButton("Build Navp", navpBuildDone.empty() && navKit->geom && navKit->obj->objLoaded))
	{
		std::thread buildNavpThread(&Navp::buildNavp, this);
		buildNavpThread.detach();
	}
	imguiEndScrollArea();
}

void Navp::finalizeLoad() {
	if (navpLoadDone.size() == 1) {
		navpLoadDone.clear();
		navpLoaded = true;
	}
	if (!navpBuildDone.empty()) {
		navpLoaded = true;
		navKit->sample->saveAll("output.navp.json");
		NavPower::NavMesh newNavMesh = LoadNavMeshFromJson("output.navp.json");
		std::swap(*navMesh, newNavMesh);
		navpBuildDone.clear();
	}
}