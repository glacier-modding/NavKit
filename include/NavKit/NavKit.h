#pragma once

#include <iostream>
#include <vector>
#include <time.h>
#include <stdlib.h>

#include <SDL.h>
#include <SDL_opengl.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>

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

#include "..\NavWeakness\NavWeakness.h"
#include "..\NavWeakness\NavPower.h"

#include "..\ResourceLib_HM3\ResourceConverter.h"
#include "..\ResourceLib_HM3\ResourceGenerator.h"
#include "..\ResourceLib_HM3\ResourceLib.h"
#include "..\ResourceLib_HM3\ResourceLibCommon.h"
#include "..\ResourceLib_HM3\ResourceLib_HM3.h"
#include "..\ResourceLib_HM3\Generated\ZHMGen.h"

#include "..\..\extern\tinyfiledialogs\tinyfiledialogs.h"

void renderNavMesh(NavPower::NavMesh* navMesh);
void renderAirg(Airg* airg);
void renderObj(InputGeom* m_geom, DebugDrawGL* m_dd);
void renderArea(NavPower::Area area);
char* openNavpFileDialog(char* lastNavpFolder);
char* openAirgFileDialog(char* lastAirgFolder);
char* openObjFileDialog(char* lastObjFolder);
char* openHitmanFolderDialog(char* lastHitmanFolder);
char* openOutputFolderDialog(char* lastOutputFolder);
char* openSceneInputDialog(char* lastScene);
