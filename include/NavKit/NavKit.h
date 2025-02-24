#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <time.h>
#include <stdlib.h>
#include <chrono>

#include <SDL.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#include <GL/glu.h>
#include <GL/glut.h>

#include <SimpleIni.h>

#include <Recast.h>
#include <RecastDebugDraw.h>

#include "..\nativefiledialog-extended\nfd.h"

#include "..\..\extern\vcpkg\packages\recastnavigation_x64-windows\include\recastnavigation\RecastAlloc.h"
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

#include "..\ConcurrentQueue\ConcurrentQueue.h"

#include "NavKitConfig.h"
#include "GameConnection.h"
#include "ReasoningGrid.h"
#include "SceneExtract.h"
#include "Navp.h"
#include "Obj.h"
#include "Airg.h"
#include "FileUtil.h"
#include "VisionData.h"
#include "Renderer.h"
#include "InputHandler.h"
#include "Gui.h"
#include <iomanip>
#include <dbghelp.h>
#include "Resource.h"
#include <SDL_syswm.h>
#include <cpptrace/from_current.hpp>
#include <stdexcept>
#pragma comment(lib, "dbghelp.lib")
#undef main

class Navp;
class Obj;
class Airg;
class SceneExtract;
class Renderer;
class InputHandler;
class GameConnection;
class Gui;

class NavKit {
public:
    NavKit();

    ~NavKit();

    SceneExtract *sceneExtract;
    Navp *navp;
    Obj *obj;
    Airg *airg;
    Renderer *renderer;
    InputHandler *inputHandler;
    Gui *gui;

    GameConnection *gameConnection;
    Sample *sample;
    BuildContext ctx;

    bool done;

    float scrollZoom;
    bool rotate;
    bool movedDuringRotate;
    float keybSpeed = 22.0f;

    InputGeom *geom;
    DebugDrawGL m_dd;

    CSimpleIniA ini;

    int runProgram(int argc, char **argv);

    static INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void log(rcLogCategory category, const char *message, ...);

    static std::string *errorMessage;

private:
    rsj::ConcurrentQueue<std::pair<rcLogCategory, std::string> > logQueue;

    void loadSettings();

    static void logRunner(NavKit *navKit);
};
