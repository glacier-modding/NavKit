#include "../../include/NavKit/module/Settings.h"

#include <filesystem>

#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"

#include "../../include/RecastDemo/imgui.h"

Settings::Settings(): ini(CSimpleIniA()), settingsScroll(0), backgroundColor(0.30f) {
}

const int Settings::SETTINGS_MENU_HEIGHT = 210;

void Settings::Load() {
    CSimpleIni &ini = getInstance().ini;
    ini.SetUnicode();
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    Obj &obj = Obj::getInstance();
    Renderer &renderer = Renderer::getInstance();

    if (const SI_Error rc = ini.LoadFile("NavKit.ini"); rc < 0) {
        Logger::log(NK_ERROR, "Error loading settings from NavKit.ini");
    } else {
        Logger::log(NK_INFO, "Loading settings from NavKit.ini...");

        sceneExtract.setHitmanFolder(ini.GetValue("Paths", "hitman", "default"));
        sceneExtract.setOutputFolder(ini.GetValue("Paths", "output", "default"));
        obj.setBlenderFile(ini.GetValue("Paths", "blender", "default"));
        Grid &grid = Grid::getInstance();
        grid.saveSpacing(static_cast<float>(atof(ini.GetValue("Airg", "spacing", "2.0f"))));
        renderer.initFrameRate(static_cast<float>(atof(ini.GetValue("Renderer", "frameRate", "-1.0f"))));
        getInstance().backgroundColor = static_cast<float>(atof(ini.GetValue("Colors", "backgroundColor", "0.16f")));
    }
}

void Settings::drawMenu() {
    Renderer &renderer = Renderer::getInstance();
    if (imguiBeginScrollArea("Settings menu", renderer.width - 250 - 10,
                             renderer.height - 10 - SETTINGS_MENU_HEIGHT, 250,
                             SETTINGS_MENU_HEIGHT, &settingsScroll)) {
        Gui::getInstance().mouseOverMenu = true;;
    }
    imguiLabel("Background color");
    if (imguiSlider("Black                      White", &backgroundColor, 0.0f, 1.0f, 0.01f)) {
        setValue("Colors", "backgroundColor", std::to_string(backgroundColor));
        save();
    }
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    imguiLabel("Set Hitman Directory");
    if (imguiButton(sceneExtract.hitmanFolderName.c_str())) {
        if (const char *folderName = sceneExtract.openHitmanFolderDialog(sceneExtract.lastHitmanFolder.data())) {
            sceneExtract.setHitmanFolder(folderName);
        }
    }
    imguiLabel("Set Output Directory");
    if (imguiButton(sceneExtract.outputFolderName.c_str())) {
        if (const char *folderName = sceneExtract.openOutputFolderDialog(sceneExtract.lastOutputFolder.data())) {
            sceneExtract.setOutputFolder(folderName);
        }
    }
    imguiLabel("Set Blender Executable");
    if (Obj &obj = Obj::getInstance(); imguiButton(obj.blenderName.c_str())) {
        if (const char *blenderFileName = obj.openSetBlenderFileDialog(obj.lastBlenderFile.data())) {
            obj.setBlenderFile(blenderFileName);
        }
    }
    imguiEndScrollArea();
}

void Settings::save() {
    if (getInstance().ini.SaveFile("NavKit.ini") == SI_FAIL) {
        Logger::log(NK_ERROR, "Error saving settings file");
    }
}

void Settings::setValue(const std::string &folder, const std::string &key, const std::string &value) {
    if (getInstance().ini.SetValue(folder.c_str(), key.c_str(), value.c_str()) == SI_FAIL) {
        Logger::log(NK_ERROR, "Error updating setting");
    }
}
