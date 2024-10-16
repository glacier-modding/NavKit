#pragma once

#include "NavKit.h"

class NavKit;

class Navp {
public:
	Navp(NavKit* navKit);
	~Navp();

	void resetDefaults();
	void renderNavMesh();
	void renderNavMeshForHitTest();
	void drawMenu();
	void finalizeLoad();
	void buildBinaryAreaToAreaMap();
	void setSelectedNavpAreaIndex(int index);

	NavPower::NavMesh* navMesh;
	std::map<NavPower::Binary::Area*, NavPower::Area*> binaryAreaToAreaMap;
	int selectedNavpAreaIndex;
	bool navpLoaded;
	bool showNavp;
	bool showNavpIndices;
	bool doNavpHitTest;
	int navpScroll;
	bool loading;

	float bBoxPosX;
	float bBoxPosY;
	float bBoxPosZ;
	float bBoxSizeX;
	float bBoxSizeY;
	float bBoxSizeZ;

	float lastBBoxPosX;
	float lastBBoxPosY;
	float lastBBoxPosZ;
	float lastBBoxSizeX;
	float lastBBoxSizeY;
	float lastBBoxSizeZ;

	bool stairsCheckboxValue;

	void setLastLoadFileName(const char* fileName);
	void setLastSaveFileName(const char* fileName);

private:
	NavKit* navKit;
	static void buildNavp(Navp* navp);
	void renderArea(NavPower::Area area, bool selected, int areaIndex);

	static bool areaIsStairs(NavPower::Area area);
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