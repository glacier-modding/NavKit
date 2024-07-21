#pragma once

#include <iostream>
#include <vector>
#include <time.h>
#include <stdlib.h>
#include <chrono>

#include <SDL.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#include <nfd.h>

#include "NavKitConfig.h"
#include "Glacier2Obj.h"
#include "GameConnection.h"

#include <Recast.h>
#include <RecastDebugDraw.h>
#include "..\..\extern\vcpkg\packages\recastnavigation_x64-windows\include\recastnavigation\RecastAlloc.h"

#include "ReasoningGrid.h"
#include "Navp.h"
#include "Obj.h"
#include "FileUtil.h"

#include "..\RecastDemo\ChunkyTriMesh.h"
#include "..\RecastDemo\imgui.h"
#include "..\RecastDemo\imguiRenderGL.h"
#include "..\RecastDemo\InputGeom.h"
#include "..\RecastDemo\MeshLoaderObj.h"
#include "..\RecastDemo\SampleInterfaces.h"
#include "..\RecastDemo\Sample.h"
#include "..\RecastDemo\Sample_SoloMesh.h"

#include "..\NavWeakness\NavWeakness.h"
#include "..\NavWeakness\NavPower.h"

#include "..\ResourceLib_HM3\ResourceConverter.h"
#include "..\ResourceLib_HM3\ResourceGenerator.h"
#include "..\ResourceLib_HM3\ResourceLib.h"
#include "..\ResourceLib_HM3\ResourceLibCommon.h"
#include "..\ResourceLib_HM3\ResourceLib_HM3.h"
#include "..\ResourceLib_HM3\Generated\ZHMGen.h"
#undef main

class Navp;
class Obj;
class NavKit {
public:
	NavKit();
	~NavKit();
	Navp* navp;
	Obj* obj;

	int width;
	int height;
	SDL_Window* window;
	SDL_Renderer* renderer;
	int runProgram(int argc, char** argv);
	GameConnection* gameConnection;
	Sample* sample = new Sample_SoloMesh();

	BuildContext ctx;

	bool mouseOverMenu = false;
	std::string airgName = "Load Airg";
	std::string lastLoadAirgFile = airgName;
	std::string saveAirgName = "Save Airg";
	std::string lastSaveAirgFile = saveAirgName;
	bool airgLoaded = false;
	std::vector<bool> airgLoadDone;
	bool showAirg = true;
	ResourceConverter* airgResourceConverter = HM3_GetConverterForResource("AIRG");;
	ResourceGenerator* airgResourceGenerator = HM3_GetGeneratorForResource("AIRG");
	ReasoningGrid* airg = new ReasoningGrid();

	std::string lastBlenderFile = "\"C:\\Program Files\\Blender Foundation\\Blender 3.4\\blender.exe\"";
	std::string blenderName = "Choose Blender app";
	bool blenderSet = false;

	std::vector<bool> extractionDone;
	bool startedObjGeneration = false;

	std::string hitmanFolderName = "Choose Hitman folder";
	std::string lastHitmanFolder = hitmanFolderName;
	bool hitmanSet = false;
	std::string outputFolderName = "Choose Output folder";
	std::string lastOutputFolder = outputFolderName;
	bool outputSet = false;
	bool showMenu = true;
	bool showLog = true;

	int extractScroll = 0;
	int navpScroll = 0;
	int airgScroll = 0;
	int objScroll = 0;
	int logScroll = 0;
	int lastLogCount = -1;

	InputGeom* geom = 0;
	DebugDrawGL m_dd;
private:
	void init();
	void initFrameBuffer(int width, int height);
	int hitTest(BuildContext* ctx, NavPower::NavMesh* navMesh, int mx, int my, int width, int height);
	void renderAirg(ReasoningGrid* airg);
	void renderObj(InputGeom* m_geom, DebugDrawGL* m_dd);
	void renderArea(NavPower::Area area, bool selected);
	char* openAirgFileDialog(char* lastAirgFolder);
	char* openSaveAirgFileDialog(char* lastAirgFolder);
	char* openLoadObjFileDialog(char* lastObjFolder);
	char* openSetBlenderFileDialog(char* lastBlenderFile);
	char* openSaveObjFileDialog(char* lastObjFolder);
	char* openHitmanFolderDialog(char* lastHitmanFolder);
	char* openOutputFolderDialog(char* lastOutputFolder);
	static void loadAirg(NavKit* navKit, char* fileName, bool isFromJson);
	void saveAirg(ReasoningGrid* airg, BuildContext* ctx, char* fileName);
	void loadObjMesh(InputGeom* geom, BuildContext* ctx, char* objToLoad, std::vector<bool>* objLoadDone);
	void copyObjFile(const std::string& from, BuildContext* ctx, const std::string& to);
	void saveObjMesh(char* objToCopy, BuildContext* ctx, char* newFileName);
};

