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
#include "../../include/NavKit/module/PersistedSettings.h"
#include <SDL.h>
#include <GL/glew.h>
#include <GL/glu.h>

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
        imguiDrawText(10, renderer.height - 20, IMGUI_ALIGN_LEFT, msg, imguiRGBA(255, 255, 255, 128));
        char cameraPosMessage[128];
        snprintf(cameraPosMessage, sizeof cameraPosMessage, "Camera position: %f, %f, %f",
                 renderer.cameraPos[0], renderer.cameraPos[1], renderer.cameraPos[2]);
        imguiDrawText(10, renderer.height - 40, IMGUI_ALIGN_LEFT, cameraPosMessage,
                      imguiRGBA(255, 255, 255, 128));
        char cameraAngleMessage[128];
        snprintf(cameraAngleMessage, sizeof cameraAngleMessage, "Camera angles: %f, %f",
                 renderer.cameraEulers[0], renderer.cameraEulers[1]);
        imguiDrawText(10, renderer.height - 60, IMGUI_ALIGN_LEFT, cameraAngleMessage,
                      imguiRGBA(255, 255, 255, 128));
        const Navp &navp = Navp::getInstance();
        char loadedNavpText[256];
        snprintf(loadedNavpText, sizeof loadedNavpText, "Loaded Navp: %s", navp.loadedNavpText.c_str());
        imguiDrawText(10, renderer.height - 80, IMGUI_ALIGN_LEFT, loadedNavpText,
                      imguiRGBA(255, 255, 255, 128));
        const Airg &airg = Airg::getInstance();
        char loadedAirgText[256];
        snprintf(loadedAirgText, sizeof loadedAirgText, "Loaded Airg: %s", airg.loadedAirgText.c_str());
        imguiDrawText(10, renderer.height - 100, IMGUI_ALIGN_LEFT, loadedAirgText,
                      imguiRGBA(255, 255, 255, 128));
        char selectedNavpAreaText[64];
        snprintf(selectedNavpAreaText, sizeof selectedNavpAreaText,
                 navp.selectedNavpAreaIndex != -1 ? "Selected Area Index: %d" : "Selected Area Index: None",
                 navp.selectedNavpAreaIndex + 1);
        imguiDrawText(10, renderer.height - 120, IMGUI_ALIGN_LEFT, selectedNavpAreaText,
                      imguiRGBA(255, 255, 255, 128));
        char selectedAirgText[64];
        snprintf(selectedAirgText, sizeof selectedAirgText,
                 airg.selectedWaypointIndex != -1 ? "Selected Waypoint Index: %d" : "Selected Waypoint Index: None",
                 airg.selectedWaypointIndex);
        imguiDrawText(10, renderer.height - 140, IMGUI_ALIGN_LEFT, selectedAirgText,
                      imguiRGBA(255, 255, 255, 128));
        const RecastAdapter &recastAdapter = RecastAdapter::getInstance();

        if (showLog) {
            if (imguiBeginScrollArea("Log", 20, 20,
                                     renderer.width - 40,
                                     renderer.height > 440
                                         ? 220
                                         : static_cast<int>(static_cast<double>(renderer.height) * 0.2),
                                     &logScroll)) {
                mouseOverMenu = true;
            }
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
            imguiEndScrollArea();
        }
    }

    imguiEndFrame();
    imguiRenderGLDraw();
}
