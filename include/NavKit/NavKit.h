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

#include <Recast.h>
#include <RecastDebugDraw.h>


#include "..\RecastDemo\ChunkyTriMesh.h"
#include "..\RecastDemo\imgui.h"
#include "..\RecastDemo\imguiRenderGL.h"
#include "..\RecastDemo\InputGeom.h"
#include "..\RecastDemo\MeshLoaderObj.h"
#include "..\RecastDemo\SampleInterfaces.h"

#include "..\NavWeakness\NavWeakness.h"
#include "..\NavWeakness\NavPower.h"

#include "..\..\extern\tinyfiledialogs\tinyfiledialogs.h"
//#include "extern\fastlz\fastlz.h"

void renderNavMesh(NavPower::NavMesh* navMesh);
void renderObj(InputGeom* m_geom, DebugDrawGL* m_dd);
void renderArea(NavPower::Area area);
char* openNavpFile(char* lastNavpFolder);
char* openObjFile(char* lastObjFolder);