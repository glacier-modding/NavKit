#include "../../include/NavKit/module/Settings.h"

#include <filesystem>

#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/SceneExtract.h"

void Settings::Load() {
    CSimpleIni &ini = getInstance().ini;
    ini.SetUnicode();
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    Obj &obj = Obj::getInstance();
    Navp &navp = Navp::getInstance();
    Airg &airg = Airg::getInstance();
    Renderer &renderer = Renderer::getInstance();

    if (const SI_Error rc = ini.LoadFile("NavKit.ini"); rc < 0) {
        Logger::log(NK_ERROR, "Error loading settings from NavKit.ini");
    } else {
        Logger::log(NK_INFO, "Loading settings from NavKit.ini...");

        sceneExtract.setHitmanFolder(ini.GetValue("Paths", "hitman", "default"));
        sceneExtract.setOutputFolder(ini.GetValue("Paths", "output", "default"));
        sceneExtract.setBlenderFile(ini.GetValue("Paths", "blender", "default"));
        float bBoxPos[3] = {
            static_cast<float>(atof(ini.GetValue("BBox", "x", "0.0f"))),
            static_cast<float>(atof(ini.GetValue("BBox", "y", "0.0f"))),
            static_cast<float>(atof(ini.GetValue("BBox", "z", "0.0f")))
        };
        float bBoxSize[3] = {
            static_cast<float>(atof(ini.GetValue("BBox", "sx", "100.0f"))),
            static_cast<float>(atof(ini.GetValue("BBox", "sy", "100.0f"))),
            static_cast<float>(atof(ini.GetValue("BBox", "sz", "100.0f")))
        };
        obj.setBBox(bBoxPos, bBoxSize);
        navp.setLastLoadFileName(ini.GetValue("Paths", "loadnavp", "default"));
        const char *fileName = ini.GetValue("Paths", "savenavp", "default");
        if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
            navp.setLastSaveFileName(fileName);
        }
        airg.setLastLoadFileName(ini.GetValue("Paths", "loadairg", "default"));
        fileName = ini.GetValue("Paths", "saveairg", "default");
        if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
            airg.setLastSaveFileName(fileName);
        }
        obj.setLastLoadFileName(ini.GetValue("Paths", "loadobj", "default"));
        fileName = ini.GetValue("Paths", "saveobj", "default");
        if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
            obj.setLastSaveFileName(fileName);
        }
        airg.saveSpacing(static_cast<float>(atof(ini.GetValue("Airg", "spacing", "2.0f"))));
        airg.saveTolerance(static_cast<float>(atof(ini.GetValue("Airg", "tolerance", "0.2f"))));
        airg.saveZSpacing(static_cast<float>(atof(ini.GetValue("Airg", "ySpacing", "1.0f"))));
        airg.saveZTolerance(static_cast<float>(atof(ini.GetValue("Airg", "zTolerance", "1.0f"))));
        renderer.initFrameRate(static_cast<float>(atof(ini.GetValue("Renderer", "frameRate", "-1.0f"))));
    }
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
