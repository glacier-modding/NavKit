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
#include "SceneExtract.h"
#include "Navp.h"
#include "Obj.h"
#include "Airg.h"
#include "FileUtil.h"
#include "Renderer.h"
#include "InputHandler.h"

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
class Airg;
class SceneExtract;
class Renderer;
class InputHandler;

class NavKit {
public:
	NavKit();
	~NavKit();

	SceneExtract* sceneExtract;
	Navp* navp;
	Obj* obj;
	Airg* airg;
	Renderer* renderer;
	InputHandler* inputHandler;
	GameConnection* gameConnection;
	Sample* sample;
	BuildContext ctx;

	bool mouseOverMenu = false;

	bool done;
	bool showMenu;
	bool showLog;
	int logScroll;
	int lastLogCount;

	float scrollZoom;
	bool rotate;
	bool movedDuringRotate;
	float keybSpeed = 22.0f;

	InputGeom* geom;
	DebugDrawGL m_dd;

	int runProgram(int argc, char** argv);
private:
	int hitTest(BuildContext* ctx, NavPower::NavMesh* navMesh, int mx, int my, int width, int height);
};

