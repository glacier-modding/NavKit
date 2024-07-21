#pragma once

#include "NavKit.h"

class NavKit;
class InputGeom;
class Obj {
public:
	Obj(NavKit* navKit);
	NavKit* navKit;
	std::string loadObjName;
	std::string saveObjName;
	std::string lastObjFileName;
	std::string lastSaveObjFileName;
	bool objLoaded;
	bool showObj;
	bool loadObj;
	std::vector<std::string> files;
	const std::string meshesFolder = "Obj";
	std::string objToLoad;
	std::vector<bool> objLoadDone;
	int objScroll;

	void copyObjFile(const std::string& from, BuildContext* ctx, const std::string& to);
	void saveObjMesh(char* objToCopy, BuildContext* ctx, char* newFileName);
	static void loadObjMesh(Obj* obj);
	void renderObj(InputGeom* m_geom, DebugDrawGL* m_dd);
	char* openLoadObjFileDialog(char* lastObjFolder);
	char* openSaveObjFileDialog(char* lastObjFolder);
	void drawMenu();
	void finalizeLoad();
};