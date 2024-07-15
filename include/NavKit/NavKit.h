#pragma once

#include <iostream>
#include <vector>
#include <time.h>
#include <stdlib.h>
#include <chrono>

#include <SDL.h>
#include <SDL_opengl.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#include <nfd.h>

#include "NavKitConfig.h"
#include "Glacier2Obj.h"

#include <Recast.h>
#include <RecastDebugDraw.h>
#include "..\..\extern\vcpkg\packages\recastnavigation_x64-windows\include\recastnavigation\RecastAlloc.h"

#include "Airg.h"

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

void renderNavMesh(NavPower::NavMesh* navMesh);
void renderAirg(Airg* airg);
void renderObj(InputGeom* m_geom, DebugDrawGL* m_dd);
void renderArea(NavPower::Area area);
char* openLoadNavpFileDialog(char* lastNavpFolder);
char* openSaveNavpFileDialog(char* lastNavpFolder);
char* openAirgFileDialog(char* lastAirgFolder);
char* openLoadObjFileDialog(char* lastObjFolder);
char* openSaveObjFileDialog(char* lastObjFolder);
char* openHitmanFolderDialog(char* lastHitmanFolder);
char* openOutputFolderDialog(char* lastOutputFolder);
void loadNavMesh(NavPower::NavMesh* navMesh, BuildContext* ctx, char* fileName, bool isFromJson);
void loadAirg(Airg* airg, BuildContext* ctx, ResourceConverter* airgResourceConverter, char* fileName, bool isFromJson, std::vector<bool>* airgLoaded);
void loadObjMesh(InputGeom* geom, BuildContext* ctx, char* objToLoad, std::vector<bool>* objLoadDone);
void copyObjFile(const std::string& from, BuildContext* ctx, const std::string& to);
void saveObjMesh(char* objToCopy, BuildContext* ctx, char* newFileName);
void buildNavp(Sample* sample, BuildContext* ctx, std::vector<bool>* navpBuildDone);