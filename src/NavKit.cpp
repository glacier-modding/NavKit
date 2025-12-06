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
#include <cpptrace/from_current.hpp>
#include "../include/NavKit/module/Airg.h"
#include "../include/NavKit/module/Gui.h"
#include "../include/NavKit/module/InputHandler.h"
#include "../include/NavKit/module/Logger.h"
#include "../include/NavKit/module/Menu.h"
#include "../include/NavKit/module/Navp.h"
#include "../include/NavKit/module/Obj.h"
#include "../include/NavKit/module/Renderer.h"
#include "../include/NavKit/module/SceneExtract.h"
#include "../include/NavKit/module/Settings.h"
#include "../include/NavKit/util/ErrorHandler.h"
#include "../include/NavKit/util/FileUtil.h"
#include "../include/NavKit/util/UpdateChecker.h"
#undef main

int SDL_main(const int argc, char **argv) {
    CPPTRACE_TRY
        {
            std::thread logThread(Logger::logRunner);
            logThread.detach();

            Settings::getInstance().load();
            Renderer &renderer = Renderer::getInstance();
            if (!renderer.initWindowAndRenderer()) {
                return -1;
            }
            SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);

            UpdateChecker &updateChecker = UpdateChecker::getInstance();
            updateChecker.startUpdateCheck();

            SceneExtract &sceneExtract = SceneExtract::getInstance();
            Navp &navp = Navp::getInstance();
            Obj &obj = Obj::getInstance();
            Airg &airg = Airg::getInstance();
            InputHandler &inputHandler = InputHandler::getInstance();
            Menu::updateMenuState();
            Gui &gui = Gui::getInstance();
            bool isRunning = true;
            Logger::log(NK_INFO, "NavKit initialized.");
            while (isRunning) {
                if (inputHandler.handleInput() == InputHandler::QUIT) {
                    isRunning = false;
                }
                renderer.renderFrame();
                inputHandler.hitTest();
                gui.drawGui();

                sceneExtract.finalizeExtractScene();
                obj.finalizeObjBuild();
                navp.finalizeBuild();
                obj.finalizeLoad();
                airg.finalizeSave();
                obj.finalizeExtractAlocs();
                renderer.finalizeFrame();
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
