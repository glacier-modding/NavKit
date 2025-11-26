#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Gui.h"

#include <deque>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"
#include <SDL.h>
#include <GL/glew.h>
#include <GL/glu.h>

#include "../../include/NavKit/module/Obj.h"
#include "../../include/RecastDemo/imgui.h"
#include "../../include/RecastDemo/imguiRenderGL.h"

Gui::Gui() {
    showMenu = true;
    showLog = true;
    logScroll = 0;
    lastLogCount = -1;
    mouseOverMenu = false;
}

void Gui::drawGui() {
    glFrontFace(GL_CCW);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const InputHandler &inputHandler = InputHandler::getInstance();
    const Renderer &renderer = Renderer::getInstance();
    gluOrtho2D(0, renderer.width, 0, renderer.height);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    mouseOverMenu = false;
    imguiBeginFrame(inputHandler.mousePos[0], inputHandler.mousePos[1],
                    inputHandler.mouseButtonMask, inputHandler.mouseScroll);
    if (showMenu) {
        constexpr char msg[] =
                "W/S/A/D/Q/E: Move  LMB: Select / Deselect RMB: Rotate  Tab: Show / Hide UI  Ctrl: Slow camera movement  Shift: Fast camera movement";
        imguiDrawText(280, renderer.height - 20, IMGUI_ALIGN_LEFT, msg, imguiRGBA(255, 255, 255, 128));
        char cameraPosMessage[128];
        snprintf(cameraPosMessage, sizeof cameraPosMessage, "Camera position: %f, %f, %f",
                 renderer.cameraPos[0], renderer.cameraPos[1], renderer.cameraPos[2]);
        imguiDrawText(280, renderer.height - 40, IMGUI_ALIGN_LEFT, cameraPosMessage,
                      imguiRGBA(255, 255, 255, 128));
        char cameraAngleMessage[128];
        snprintf(cameraAngleMessage, sizeof cameraAngleMessage, "Camera angles: %f, %f",
                 renderer.cameraEulers[0], renderer.cameraEulers[1]);
        imguiDrawText(280, renderer.height - 60, IMGUI_ALIGN_LEFT, cameraAngleMessage,
                      imguiRGBA(255, 255, 255, 128));
        char selectedNavpText[64];
        Navp &navp = Navp::getInstance();
        snprintf(selectedNavpText, sizeof selectedNavpText,
                 navp.selectedNavpAreaIndex != -1 ? "Selected Area Index: %d" : "Selected Area Index: None",
                 navp.selectedNavpAreaIndex + 1);
        imguiDrawText(280, renderer.height - 80, IMGUI_ALIGN_LEFT, selectedNavpText,
                      imguiRGBA(255, 255, 255, 128));
        char selectedAirgText[64];
        Airg &airg = Airg::getInstance();
        snprintf(selectedAirgText, sizeof selectedAirgText,
                 airg.selectedWaypointIndex != -1 ? "Selected Waypoint Index: %d" : "Selected Waypoint Index: None",
                 airg.selectedWaypointIndex);
        imguiDrawText(280, renderer.height - 100, IMGUI_ALIGN_LEFT, selectedAirgText,
                      imguiRGBA(255, 255, 255, 128));
        SceneExtract &sceneExtract = SceneExtract::getInstance();
        Obj &obj = Obj::getInstance();
        Scene::getInstance();
        Settings &settings = Settings::getInstance();
        navp.drawMenu();
        airg.drawMenu();
        settings.drawMenu();

        const int consoleHeight = showLog ? 220 : 60;
        const int consoleWidth = showLog ? renderer.width - 310 - 250 : 100;
        if (showLog) {
            if (imguiBeginScrollArea("Log", 250 + 20, 20, consoleWidth, consoleHeight, &logScroll))
                mouseOverMenu = true;
            const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
            std::lock_guard lock(recastAdapter.getLogMutex());

            const std::deque<std::string> &logBuffer = recastAdapter.getLogBuffer();
            for (auto it = logBuffer.cbegin();
                 it != logBuffer.cend(); ++it) {
                imguiLabel(it->c_str());
            }
            if (const int logCount = recastAdapter.getLogCount(); lastLogCount != logCount) {
                logScroll = std::max(0, logCount * 20 - 160);
                lastLogCount = logCount;
            }
        }

        imguiEndScrollArea();
    }

    imguiEndFrame();
    imguiRenderGLDraw();
}
