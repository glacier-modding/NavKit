#include "..\include\NavKit\Navp.h"

Navp::Navp(NavKit* navKit): navKit(navKit) {
	loadNavpName = "Load Navp";
	lastLoadNavpFile = loadNavpName;
	saveNavpName = "Save Navp";
	lastSaveNavpFile = saveNavpName;
	navpLoaded = false;
	showNavp = true;
	showNavpIndices = false;
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
	lastBBoxPosX = 0.0;
	lastBBoxPosY = 0.0;
	lastBBoxPosZ = 0.0;
	lastBBoxSizeX = 100.0;
	lastBBoxSizeY = 100.0;
	lastBBoxSizeZ = 100.0;

	stairsCheckboxValue = false;
	loading = false;
}

Navp::~Navp() {
}

void Navp::resetDefaults() {
	bBoxPosX = 0.0;
	bBoxPosY = 0.0;
	bBoxPosZ = 0.0;
	bBoxSizeX = 100.0;
	bBoxSizeY = 100.0;
	bBoxSizeZ = 100.0;
	lastBBoxPosX = 0.0;
	lastBBoxPosY = 0.0;
	lastBBoxPosZ = 0.0;
	lastBBoxSizeX = 100.0;
	lastBBoxSizeY = 100.0;
	lastBBoxSizeZ = 100.0;
}

char* Navp::openLoadNavpFileDialog(char* lastNavpFolder) {
	nfdu8filteritem_t filters[2] = { { "Navp files", "navp" }, { "Navp.json files", "navp.json" } };
	return FileUtil::openNfdLoadDialog(filters, 2, lastNavpFolder);
}

char* Navp::openSaveNavpFileDialog(char* lastNavpFolder) {
	nfdu8filteritem_t filters[2] = { { "Navp files", "navp" }, { "Navp.json files", "navp.json" } };
	return FileUtil::openNfdSaveDialog(filters, 2, "output", lastNavpFolder);
}

bool Navp::areaIsStairs(NavPower::Area area) {
	return area.m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_STEPS;
}

void Navp::renderArea(NavPower::Area area, bool selected, int areaIndex) {
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
	if (index == -1 && selectedNavpAreaIndex != -1) {
		navKit->log(RC_LOG_PROGRESS, ("Deselected area: " + std::to_string(selectedNavpAreaIndex)).c_str());
	}
	selectedNavpAreaIndex = index;
	if (index != -1 && index < navMesh->m_areas.size()) {
		navKit->log(RC_LOG_PROGRESS, ("Selected area: " + std::to_string(index)).c_str());
		Vec3 pos = navMesh->m_areas[index].m_area->m_pos;
		char v[16];
		snprintf(v, sizeof(v), "%.2f", pos.X);
		std::string msg = "Area center:";
		msg += " X: " + std::string{ v };
		snprintf(v, sizeof(v), "%.2f", pos.Y);
		msg += " Y: " + std::string{ v };
		snprintf(v, sizeof(v), "%.2f", pos.Z);
		msg += " Z: " + std::string{ v };
		int edgeIndex = 0;
		for (auto vertex : navMesh->m_areas[index].m_edges) {
			msg += " Edge " + std::to_string(edgeIndex) + " pos:";
			snprintf(v, sizeof(v), "%.2f", vertex->m_pos.X);
			msg += " X: " + std::string{ v };
			snprintf(v, sizeof(v), "%.2f", vertex->m_pos.Y);
			msg += " Y: " + std::string{ v };
			snprintf(v, sizeof(v), "%.2f", vertex->m_pos.Z);
			msg += " Z: " + std::string{ v };
			edgeIndex++;
		}
		navKit->log(RC_LOG_PROGRESS, (msg).c_str());
	}
}

void Navp::renderNavMesh() {
	if (!loading) {
		int areaIndex = 0;
		for (const NavPower::Area& area : navMesh->m_areas) {
			renderArea(area, areaIndex == selectedNavpAreaIndex, areaIndex);
			areaIndex++;
		}
		if (showNavpIndices) {
			areaIndex = 0;
			for (const NavPower::Area& area : navMesh->m_areas) {
				navKit->renderer->drawText(std::to_string(areaIndex), { area.m_area->m_pos.X, area.m_area->m_pos.Z + 0.1f, -area.m_area->m_pos.Y }, { .7f, .7f, 1 });
				if (selectedNavpAreaIndex == areaIndex) {
					int edgeIndex = 0;
					for (auto vertex : area.m_edges) {
						navKit->renderer->drawText(std::to_string(edgeIndex), { vertex->m_pos.X, vertex->m_pos.Z + 0.1f, -vertex->m_pos.Y }, { .7f, 1, .7f });
						edgeIndex++;
					}
				}
				areaIndex++;
			}
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
	navp->navpLoaded = true;
	navp->buildBinaryAreaToAreaMap();
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

void Navp::setLastLoadFileName(const char* fileName) {
	if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
		loadNavpName = fileName;
		lastLoadNavpFile = loadNavpName.data();
		loadNavpName = loadNavpName.substr(loadNavpName.find_last_of("/\\") + 1);
		navKit->ini.SetValue("Paths", "loadnavp", fileName);
		navKit->ini.SaveFile("NavKit.ini");
	}
}
	
void Navp::setLastSaveFileName(const char* fileName) {
	saveNavpName = fileName;
	lastSaveNavpFile = saveNavpName.data();
	saveNavpName = saveNavpName.substr(saveNavpName.find_last_of("/\\") + 1);
	navKit->ini.SetValue("Paths", "savenavp", fileName);
	navKit->ini.SaveFile("NavKit.ini");
}
	
void Navp::drawMenu() {
	int navpMenuHeight = std::min(1165, navKit->renderer->height - 20);
	if (imguiBeginScrollArea("Navp menu", 10, navKit->renderer->height - navpMenuHeight - 10, 250, navpMenuHeight, &navpScroll))
		navKit->gui->mouseOverMenu = true;
	if (imguiCheck("Show Navp", showNavp))
		showNavp = !showNavp;
	if (imguiCheck("Show Navp Indices", showNavpIndices))
		showNavpIndices = !showNavpIndices;
	imguiLabel("Load Navp from file");
	if (imguiButton(loadNavpName.c_str(), navpLoadDone.empty())) {
		char* fileName = openLoadNavpFileDialog(lastLoadNavpFile.data());
		if (fileName){
			setLastLoadFileName(fileName);
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
		if (fileName){
			setLastSaveFileName(fileName);
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
			navKit->log(RC_LOG_PROGRESS, "Done saving Navp.");
		}
	}
	if (imguiButton("Send Navp to game", navpLoaded)) {
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
			std::string newTypeString = (navMesh->m_areas[selectedNavpAreaIndex].m_area->m_usageFlags == NavPower::AreaUsageFlags::AREA_STEPS) ? "AREA_FLAT" : "AREA_STEPS";
			navKit->log(RC_LOG_PROGRESS, ("Setting area type to: " + newTypeString).c_str());
			navMesh->m_areas[selectedNavpAreaIndex].m_area->m_usageFlags = newType;
		}
	}

	imguiSeparatorLine();

	navKit->sample->handleCommonSettings();

	bool bboxChanged = false;
	if (imguiSlider("Bounding Box Origin X", &bBoxPosX, -500.0f, 500.0f, 1.0f)) {
		if (lastBBoxPosX != bBoxPosX) {
			bboxChanged = true;
			lastBBoxPosX = bBoxPosX;
		}
	}
	if (imguiSlider("Bounding Box Origin Y", &bBoxPosY, -500.0f, 500.0f, 1.0f)) {
		if (lastBBoxPosY != bBoxPosY) {
			bboxChanged = true;
			lastBBoxPosY = bBoxPosY;
		}
	}
	if (imguiSlider("Bounding Box Origin Z", &bBoxPosZ, -500.0f, 500.0f, 1.0f)) {
		if (lastBBoxPosZ != bBoxPosZ) {
			bboxChanged = true;
			lastBBoxPosZ = bBoxPosZ;
		}
	}
	if (imguiSlider("Bounding Box Size X", &bBoxSizeX, 1.0f, 800.0f, 1.0f)) {
		if (lastBBoxSizeX != bBoxSizeX) {
			bboxChanged = true;
			lastBBoxSizeX = bBoxSizeX;
		}
	}
	if (imguiSlider("Bounding Box Size Y", &bBoxSizeY, 1.0f, 800.0f, 1.0f)) {
		if (lastBBoxSizeY != bBoxSizeY) {
			bboxChanged = true;
			lastBBoxSizeY = bBoxSizeY;
		}
	}
	if (imguiSlider("Bounding Box Size Z", &bBoxSizeZ, 1.0f, 800.0f, 1.0f)) {
		if (lastBBoxSizeZ != bBoxSizeZ) {
			bboxChanged = true;
			lastBBoxSizeZ = bBoxSizeZ;
		}
	}
	if (imguiButton("Reset Defaults")) {
		resetDefaults();
		navKit->sample->resetCommonSettings();
		float bBoxPos[3] = { bBoxPosX, bBoxPosY, bBoxPosZ };
		float bBoxSize[3] = { bBoxSizeX, bBoxSizeY, bBoxSizeZ };
		navKit->obj->setBBox(bBoxPos, bBoxSize);
	}

	if (bboxChanged) 
	{
		float bBoxPos[3] = { bBoxPosX, bBoxPosY, bBoxPosZ };
		float bBoxSize[3] = { bBoxSizeX, bBoxSizeY, bBoxSizeZ };
		navKit->obj->setBBox(bBoxPos, bBoxSize);
	}
	if (imguiButton("Build Navp from Obj", navpBuildDone.empty() && navKit->geom && navKit->obj->objLoaded))
	{
		std::thread buildNavpThread(&Navp::buildNavp, this);
		buildNavpThread.detach();
	}
	imguiEndScrollArea();
}

void Navp::buildBinaryAreaToAreaMap() {
	binaryAreaToAreaMap.clear();
	for (NavPower::Area& area : navMesh->m_areas) {
		binaryAreaToAreaMap.emplace(area.m_area, &area);
	}
}

void Navp::finalizeLoad() {
	if (!navpBuildDone.empty()) {
		navpLoaded = true;
		navKit->sample->saveAll("output.navp.json");
		NavPower::NavMesh newNavMesh = LoadNavMeshFromJson("output.navp.json");
		std::swap(*navMesh, newNavMesh);
		navpBuildDone.clear();
		buildBinaryAreaToAreaMap();
	}
}