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
#include <wx/process.h>
#include <wx/utils.h>

#include <charconv>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

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

#include "../include/NavKit/module/NavKitSettings.h"
#include "../include/NavKit/module/WxApplication.h"

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
    processWxEvents();
    if (InputHandler::getInstance().handleInput() == InputHandler::QUIT) {
        return false;
    }
    runFrameIteration();
    return true;
}

int SDL_main(const int argc, char** argv) {
    if (argc == 5 && std::string_view(argv[1]) == "--cleanup-update") {
        long updaterPid = 0;
        const char* pidEnd = argv[2] + std::char_traits<char>::length(argv[2]);
        if (const auto [end, error] = std::from_chars(argv[2], pidEnd, updaterPid);
            error != std::errc{} || end != pidEnd || updaterPid <= 0) {
            return 1;
        }
        while (wxProcess::Exists(static_cast<int>(updaterPid))) {
            wxMilliSleep(100);
        }

        const std::filesystem::path updaterPath = std::filesystem::u8path(argv[3]);
        const std::filesystem::path updaterDirectory = std::filesystem::u8path(argv[4]);
        std::error_code error;
        const std::filesystem::path tempDirectory = std::filesystem::temp_directory_path(error);
        if (error || updaterPath.filename() != "updater.exe" ||
            updaterPath.parent_path().lexically_normal() != updaterDirectory.lexically_normal() ||
            updaterDirectory.parent_path().lexically_normal() != tempDirectory.lexically_normal() ||
            !updaterDirectory.filename().string().starts_with("NavKitUpdate_")) {
            return 1;
        }

        std::filesystem::remove(updaterPath, error);
        if (error) {
            return 1;
        }
        std::filesystem::remove_all(updaterDirectory, error);
        return error ? 1 : 0;
    }

    CPPTRACE_TRY {
        int wxArgc = argc;
        if (!initializeWx(wxArgc, argv)) {
            std::cerr << "Could not initialize wxWidgets.\n";
            return -1;
        }
        Logger::getInstance().start();

        PersistedSettings::getInstance().load();
        Renderer& renderer = Renderer::getInstance();
        if (!renderer.initWindowAndRenderer()) {
            shutdownWx();
            return -1;
        }
        renderer.initShaders();

        UpdateChecker& updateChecker = UpdateChecker::getInstance();
        updateChecker.startUpdateCheck();

        Menu::updateMenuState();
        bool isRunning = true;
        Logger::log(NK_INFO, "NavKit initialized.");
        renderer.renderFrame();
        Gui::getInstance().drawGui();
        renderer.finalizeFrame();
        showMainWindow();
        if (NavKitSettings& navKitSettings = NavKitSettings::getInstance(); navKitSettings.shouldOpenSettingsDialog) {
            navKitSettings.showNavKitSettingsDialog();
        }
        while (isRunning) {
            isRunning = mainLoopIteration();
        }

        NFD_Quit();
        renderer.closeWindow();
        updateChecker.waitForUpdateCheck();
        Logger::getInstance().stop();
        shutdownWx();
        return 0;
    }
    CPPTRACE_CATCH(const std::exception& e) {
        ErrorHandler::openErrorDialog("An unexpected error occurred: " + std::string(e.what()) + "\n\nStack Trace:\n" +
            cpptrace::from_current_exception().to_string());
    }
    catch (...) {
        ErrorHandler::openErrorDialog(
            "An unexpected error occurred:\n\nStack Trace: \n" + cpptrace::from_current_exception().to_string());
    }
    return 0;
}

#if !defined(_WIN32)
// SDL2main only supplies main() on platforms that need it (Windows, iOS, Android...).
int main(const int argc, char** argv) {
    return SDL_main(argc, argv);
}
#endif
