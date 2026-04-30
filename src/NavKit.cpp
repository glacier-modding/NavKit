/**
 * Copyright (c) 2025 Daniel Bierek
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/
#include <SDL.h>
#include <SDL_syswm.h>
#include <cpptrace/from_current.hpp>
#include "../include/NavKit/module/Airg.h"
#include "../include/NavKit/module/Gui.h"
#include "../include/NavKit/module/InputHandler.h"
#include "../include/NavKit/module/Logger.h"
#include "../include/NavKit/module/Menu.h"
#include "../include/NavKit/module/Navp.h"
#include "../include/NavKit/module/SceneMesh.h"
#include "../include/NavKit/module/PersistedSettings.h"
#include "../include/NavKit/module/Renderer.h"
#include "../include/NavKit/module/SceneExtract.h"
#include "../include/NavKit/util/ErrorHandler.h"
#include "../include/NavKit/util/FileUtil.h"
#include "../include/NavKit/util/UpdateChecker.h"
#include <windows.h>

#undef main

void runFrameIteration() {
    Renderer::getInstance().renderFrame();
    InputHandler::getInstance().hitTest();
    Gui::getInstance().drawGui();

    SceneExtract::getInstance().finalizeExtractScene();
    SceneMesh::getInstance().finalizeSceneMeshBuild();
    Navp::getInstance().finalizeBuild();
    SceneMesh::getInstance().finalizeLoad();
    Airg::getInstance().finalizeSave();
    SceneMesh::getInstance().finalizeExtractResources();
    Renderer::getInstance().finalizeFrame();
}

bool mainLoopIteration() {
    if (InputHandler::getInstance().handleInput() == InputHandler::QUIT) {
        return false;
    }
    runFrameIteration();
    return true;
}

static int SDLCALL eventFilter(void* userdata, SDL_Event* event) {
    if (event->type == SDL_SYSWMEVENT) {
        const SDL_SysWMmsg* wmMsg = event->syswm.msg;
        if (wmMsg->subsystem == SDL_SYSWM_WINDOWS) {
            if (wmMsg->msg.win.msg == WM_ENTERMENULOOP) {
                SetTimer(wmMsg->msg.win.hwnd, 1, 1, nullptr);
            } else if (wmMsg->msg.win.msg == WM_EXITMENULOOP) {
                KillTimer(wmMsg->msg.win.hwnd, 1);
            } else if (wmMsg->msg.win.msg == WM_TIMER) {
                runFrameIteration();
            }
        }
    }
    return 1;
}

int SDL_main(const int argc, char** argv) {
    CPPTRACE_TRY
        {
            std::thread logThread(Logger::logRunner);
            logThread.detach();

            PersistedSettings::getInstance().load();
            Renderer& renderer = Renderer::getInstance();
            if (!renderer.initWindowAndRenderer()) {
                return -1;
            }
            renderer.initShaders();
            SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
            SDL_SetEventFilter(eventFilter, nullptr);

            UpdateChecker& updateChecker = UpdateChecker::getInstance();
            updateChecker.startUpdateCheck();

            Menu::updateMenuState();
            bool isRunning = true;
            Logger::log(NK_INFO, "NavKit initialized.");
            while (isRunning) {
                isRunning = mainLoopIteration();
            }

            NFD_Quit();
            renderer.closeWindow();
            return 0;
        }
    CPPTRACE_CATCH(const std::exception& e) {
        ErrorHandler::openErrorDialog("An unexpected error occurred: " + std::string(e.what()) + "\n\nStack Trace:\n" +
            cpptrace::from_current_exception().to_string());
    } catch (...) {
        ErrorHandler::openErrorDialog("An unexpected error occurred:\n\nStack Trace: \n" +
            cpptrace::from_current_exception().to_string());
    }
    return 0;
}
