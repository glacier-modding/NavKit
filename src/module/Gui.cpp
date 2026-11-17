#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/module/Gfxf.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include <deque>
#include <string>

#include <GL/glew.h>
#include "../../include/NavKit/module/Logger.h"
#include "../../include/RecastDemo/imgui.h"
#include "../../include/RecastDemo/imguiRenderGL.h"

namespace {
    std::string fileNameFromPath(const std::string& path) {
        const size_t separator = path.find_last_of("/\\");
        return separator == std::string::npos ? path : path.substr(separator + 1);
    }
} // namespace

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
    const InputHandler& inputHandler = InputHandler::getInstance();
    const Renderer& renderer = Renderer::getInstance();
    mouseOverMenu = false;
    imguiBeginFrame(
        inputHandler.mousePos[0], inputHandler.mousePos[1], inputHandler.mouseButtonMask, inputHandler.mouseScroll);
    if (showMenu) {
        constexpr char msg[] = "W/S/A/D/Q/E: Move  LMB: Select / Deselect RMB: Rotate  Tab: Show / Hide UI  Ctrl: Slow "
                               "camera movement  Shift: Fast camera movement";
        imguiDrawText(10, renderer.height - 20, IMGUI_ALIGN_LEFT, msg, imguiRGBA(255, 255, 255, 128));
        char cameraPosMessage[128];
        snprintf(cameraPosMessage, sizeof cameraPosMessage, "Camera position: %f, %f, %f", renderer.cameraPos[0],
            renderer.cameraPos[1], renderer.cameraPos[2]);
        imguiDrawText(10, renderer.height - 40, IMGUI_ALIGN_LEFT, cameraPosMessage, imguiRGBA(255, 255, 255, 128));
        char cameraAngleMessage[128];
        snprintf(cameraAngleMessage, sizeof cameraAngleMessage, "Camera angles: %f, %f", renderer.cameraEulers[0],
            renderer.cameraEulers[1]);
        imguiDrawText(10, renderer.height - 60, IMGUI_ALIGN_LEFT, cameraAngleMessage, imguiRGBA(255, 255, 255, 128));
        const Navp& navp = Navp::getInstance();
        char loadedNavpText[256];
        snprintf(loadedNavpText, sizeof loadedNavpText, "Loaded Navp: %s", navp.loadedNavpText.c_str());
        imguiDrawText(10, renderer.height - 80, IMGUI_ALIGN_LEFT, loadedNavpText, imguiRGBA(255, 255, 255, 128));
        const Airg& airg = Airg::getInstance();
        char loadedAirgText[256];
        snprintf(loadedAirgText, sizeof loadedAirgText, "Loaded Airg: %s", airg.loadedAirgText.c_str());
        imguiDrawText(10, renderer.height - 100, IMGUI_ALIGN_LEFT, loadedAirgText, imguiRGBA(255, 255, 255, 128));
        int resourceTextY = renderer.height - 120;
        const Crmd& crmd = Crmd::getInstance();
        if (crmd.crmdLoaded && !crmd.loadedCrmdText.empty()) {
            const std::string loadedCrmdText = "Loaded Crmd: " + fileNameFromPath(crmd.loadedCrmdText);
            imguiDrawText(10, resourceTextY, IMGUI_ALIGN_LEFT, loadedCrmdText.c_str(), imguiRGBA(255, 255, 255, 128));
            resourceTextY -= 20;
        }
        const Gfxf& gfxf = Gfxf::getInstance();
        if (gfxf.gfxfLoaded && !gfxf.loadedFile.empty()) {
            const std::string loadedGfxfText = "Loaded GFXF: " + fileNameFromPath(gfxf.loadedFile);
            imguiDrawText(10, resourceTextY, IMGUI_ALIGN_LEFT, loadedGfxfText.c_str(), imguiRGBA(255, 255, 255, 128));
            resourceTextY -= 20;
        }
        char selectedNavpAreaText[64];
        snprintf(selectedNavpAreaText, sizeof selectedNavpAreaText,
            navp.selectedNavpAreaIndex != -1 ? "Selected Area Index: %d" : "Selected Area Index: None",
            navp.selectedNavpAreaIndex + 1);
        imguiDrawText(10, resourceTextY, IMGUI_ALIGN_LEFT, selectedNavpAreaText, imguiRGBA(255, 255, 255, 128));
        resourceTextY -= 20;
        char selectedAirgText[64];
        snprintf(selectedAirgText, sizeof selectedAirgText,
            airg.selectedWaypointIndex != -1 ? "Selected Waypoint Index: %d" : "Selected Waypoint Index: None",
            airg.selectedWaypointIndex);
        imguiDrawText(10, resourceTextY, IMGUI_ALIGN_LEFT, selectedAirgText, imguiRGBA(255, 255, 255, 128));

        if (showLog) {
            const int logAreaHeight =
                renderer.height > 440 ? 250 : static_cast<int>(static_cast<double>(renderer.height) * 0.2);
            Logger& logger = Logger::getInstance();
            std::lock_guard lock(logger.getLogMutex());
            const std::deque<std::string>& logBuffer = logger.getLogBuffer();
            const int totalLogCount = logger.getLogCount();
            constexpr int lineHeight = 20;
            constexpr int headerHeight = 25;
            constexpr int padding = 10;
            const int visibleHeight = logAreaHeight - headerHeight;
            const int contentHeight = static_cast<int>(logBuffer.size()) * lineHeight + padding;
            const int maxScroll = std::max(0, contentHeight - visibleHeight);
            if (lastLogCount != totalLogCount) {
                const int addedLogs = (lastLogCount == -1) ? totalLogCount : (totalLogCount - lastLogCount);
                const int prevBufferSize = std::max(0, static_cast<int>(logBuffer.size()) - addedLogs);
                const int prevMaxScroll = std::max(0, (prevBufferSize * lineHeight + padding) - visibleHeight);
                if (constexpr int threshold = 1; lastLogCount == -1 || logScroll >= prevMaxScroll - threshold ||
                    logScroll >= maxScroll - threshold) {
                    logScroll = maxScroll;
                }
                lastLogCount = totalLogCount;
            }
            if (imguiBeginScrollArea("Log", 20, 20, renderer.width - 40, logAreaHeight, &logScroll)) {
                mouseOverMenu = true;
            }
            for (auto it = logBuffer.cbegin(); it != logBuffer.cend(); ++it) {
                imguiLabel(it->c_str());
            }
            imguiEndScrollArea();
        }
    }

    imguiEndFrame();
    imguiRenderGLDraw();
}
