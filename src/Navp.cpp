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
	selectedNavpAreaIndex = -1;
	navpScroll = 0;
	bBoxPosX = 0.0;
	bBoxPosY = 0.0;
	bBoxPosZ = 0.0;
	bBoxSizeX = 100.0;
	bBoxSizeY = 100.0;
	bBoxSizeZ = 100.0;

	stairsCheckboxValue = false;
	loading = false;
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

bool Navp::areaIsStairs(NavPower::Area area) {
	return area.m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_STEPS;
}

void Navp::renderArea(NavPower::Area area, bool selected) {
	if (areaIsStairs(area)) {
		if (!selected) {
			glColor4f(0.5, 0.0, 0.5, 0.6);
		}
		else {
			glColor4f(1.0, 0.0, 1.0, 0.3);
		}
	}
	else {
		if (!selected) {
			glColor4f(0.0, 0.0, 0.5, 0.6);
		}
		else {
			glColor4f(0.0, 0.5, 0.5, 0.6);
		}
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

void Navp::setSelectedNavpAreaIndex(int index) {
	selectedNavpAreaIndex = index;
	if (index != -1 && index < navMesh->m_areas.size()) {
		int edgeIndex = 0;
		for (auto edge : navMesh->m_areas[index].m_edges) {
			navKit->log(RC_LOG_PROGRESS, "Vertex / Edge: %d Flags: %d", edgeIndex, edge->m_flags2);
			edgeIndex++;
		}
	}
}

void Navp::renderNavMesh() {
	if (!loading) {
		int areaIndex = 0;
		for (const NavPower::Area& area : navMesh->m_areas) {
			renderArea(area, areaIndex == selectedNavpAreaIndex);
			areaIndex++;
		}
	}
}

void Navp::renderNavMeshForHitTest() {
	if (!loading) {
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
}

void Navp::loadNavMesh(Navp* navp, char* fileName, bool isFromJson) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string msg = "Loading Navp from file at ";
	msg += std::ctime(&start_time);
	navp->navKit->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();
	navp->loading = true;
	try {
		NavPower::NavMesh newNavMesh = isFromJson ? LoadNavMeshFromJson(fileName) : LoadNavMeshFromBinary(fileName);
		std::swap(*navp->navMesh, newNavMesh);
	}
	catch (...) {
		msg = "Invalid Navp file: '";
		msg += fileName;
		msg += "'...";
		navp->navKit->log(RC_LOG_ERROR, msg.data());
		return;
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
	msg = "Finished loading Navp in ";
	msg += std::to_string(duration.count());
	msg += " seconds";
	navp->navKit->log(RC_LOG_PROGRESS, msg.data());
	navp->setSelectedNavpAreaIndex(-1);
	navp->loading = false;
}

void Navp::buildNavp(Navp* navp) {
	std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::string msg = "Building Navp at ";
	msg += std::ctime(&start_time);
	navp->navKit->log(RC_LOG_PROGRESS, msg.data());
	auto start = std::chrono::high_resolution_clock::now();
	if (navp->navKit->sample->handleBuild()) {
		auto end = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
		navp->navpBuildDone.push_back(true);
		msg = "Finished building Navp in ";
		msg += std::to_string(duration.count());
		msg += " seconds";
		navp->navKit->log(RC_LOG_PROGRESS, msg.data());
		navp->setSelectedNavpAreaIndex(-1);
	}
	else {
		navp->navKit->log(RC_LOG_ERROR, "Error building Navp");
	}
}
	
void Navp::drawMenu() {
	int navpMenuHeight = std::min(1140, navKit->renderer->height - 20);
	if (imguiBeginScrollArea("Navp menu", 10, navKit->renderer->height - navpMenuHeight - 10, 250, navpMenuHeight, &navpScroll))
		navKit->gui->mouseOverMenu = true;
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
				navKit->log(RC_LOG_PROGRESS, msg.data());
				std::thread loadNavMeshThread(&Navp::loadNavMesh, this, lastLoadNavpFile.data(), true);
				loadNavMeshThread.detach();
			}
			else if (extension == "NAVP") {
				navpLoaded = true;
				std::string msg = "Loading Navp file: '";
				msg += fileName;
				msg += "'...";
				navKit->log(RC_LOG_PROGRESS, msg.data());
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
			navKit->log(RC_LOG_PROGRESS, msg.data());
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
		navKit->gameConnection = new GameConnection(navKit);
		navKit->gameConnection->SendNavp(navMesh);
		navKit->gameConnection->HandleMessages();
	}
	imguiSeparatorLine();
	imguiLabel("Selected Area");
	char selectedNavpText[64];
	snprintf(selectedNavpText, 64, selectedNavpAreaIndex != -1 ? "Area Index: %d" : "Area Index: None", selectedNavpAreaIndex);
	imguiValue(selectedNavpText);
	
	if (imguiCheck("Stairs", 
		selectedNavpAreaIndex == -1 ? false : 
			(selectedNavpAreaIndex < navMesh->m_areas.size() ? areaIsStairs(navMesh->m_areas[selectedNavpAreaIndex]) : false),
		selectedNavpAreaIndex != -1)) {
		if (selectedNavpAreaIndex != -1) {
			NavPower::AreaUsageFlags newType = (navMesh->m_areas[selectedNavpAreaIndex].m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_STEPS) ? NavPower::AreaUsageFlags::AREA_FLAT : NavPower::AreaUsageFlags::AREA_STEPS;
			navKit->log(RC_LOG_PROGRESS, "Setting area type to: %d", newType);
			navMesh->m_areas[selectedNavpAreaIndex].m_area->m_usageFlags = newType;
		}
	}

	imguiSeparatorLine();

	navKit->sample->handleCommonSettings();

	imguiSlider("Bounding Box Origin X", &bBoxPosX, -300.0f, 300.0f, 1.0f);
	imguiSlider("Bounding Box Origin Y", &bBoxPosZ, -300.0f, 300.0f, 1.0f);
	imguiSlider("Bounding Box Origin Z", &bBoxPosY, -300.0f, 300.0f, 1.0f);
	imguiSlider("Bounding Box Size X", &bBoxSizeX, 1.0f, 600.0f, 1.0f);
	imguiSlider("Bounding Box Size Y", &bBoxSizeZ, 1.0f, 600.0f, 1.0f);
	imguiSlider("Bounding Box Size Z", &bBoxSizeY, 1.0f, 600.0f, 1.0f);

	float bBoxPos[3] = { bBoxPosX, bBoxPosY, bBoxPosZ };
	float bBoxSize[3] = { bBoxSizeX, bBoxSizeY, bBoxSizeZ };
	navKit->obj->setBBox(bBoxPos, bBoxSize);
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