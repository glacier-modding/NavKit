#pragma once

#include "NavKit.h"

class NavKit;
class Navp {
public:
	Navp(NavKit* navKit);
	~Navp();

	void renderNavMesh();
	void renderNavMeshForHitTest();
	void drawMenu();
	void finalizeLoad();

	NavPower::NavMesh* navMesh;
	int selectedNavpArea;
	bool navpLoaded;
	bool showNavp;
	bool doNavpHitTest;
	int navpScroll;

private:
	NavKit* navKit;
	static void buildNavp(Navp* navp);
	void renderArea(NavPower::Area area, bool selected);

	static void loadNavMesh(Navp* navp, char* fileName, bool isFromJson);
	static char* openLoadNavpFileDialog(char* lastNavpFolder);
	static char* openSaveNavpFileDialog(char* lastNavpFolder);

	std::string loadNavpName;
	std::string lastLoadNavpFile;
	std::string saveNavpName;
	std::string lastSaveNavpFile;
	std::vector<bool> navpLoadDone;
	std::vector<bool> navpBuildDone;
};