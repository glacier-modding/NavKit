#include "../../include/NavKit/module/PersistedSettings.h"

#include <filesystem>
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"

PersistedSettings::PersistedSettings() : ini(CSimpleIniA()) {
}

void PersistedSettings::load() {
    ini.SetUnicode();
    Renderer &renderer = Renderer::getInstance();

    const std::string iniFileName = "NavKit.ini";
    if (const SI_Error rc = ini.LoadFile(iniFileName.c_str()); rc < 0) {
        Logger::log(NK_ERROR, "Error loading settings from %s", iniFileName.c_str());
    } else {
        const std::filesystem::path fullPath = std::filesystem::absolute(iniFileName);
        Logger::log(NK_INFO, "Loading settings from %s...", fullPath.string().c_str());
        NavKitSettings &navKitSettings = NavKitSettings::getInstance();
        navKitSettings.setHitmanFolder(ini.GetValue("NavKit", "hitman", "default"));
        navKitSettings.setOutputFolder(ini.GetValue("NavKit", "output", "default"));
        navKitSettings.setBlenderFile(ini.GetValue("NavKit", "blender", "default"));
        Grid &grid = Grid::getInstance();
        grid.saveSpacing(static_cast<float>(atof(ini.GetValue("Airg", "spacing", "2.0f"))));
        renderer.initFrameRate(static_cast<float>(atof(ini.GetValue("Renderer", "frameRate", "-1.0f"))));
        navKitSettings.backgroundColor = static_cast<float>(atof(ini.GetValue("Colors", "backgroundColor", "0.16f")));

        Obj &obj = Obj::getInstance();
        const char *meshTypeStr = ini.GetValue("Obj", "MeshTypeForBuild", "ALOC");
        if (strcmp(meshTypeStr, "PRIM") == 0) {
            obj.meshTypeForBuild = PRIM;
        } else {
            obj.meshTypeForBuild = ALOC;
        }

        const char *primLodsStr = ini.GetValue("Obj", "PrimLods", "11111111");
        for (int i = 0; i < 8; ++i) {
            if (i < strlen(primLodsStr)) {
                obj.primLods[i] = primLodsStr[i] == '1';
            } else {
                obj.primLods[i] = true;
            }
        }
    }
}

void PersistedSettings::save() const {
    if (ini.SaveFile("NavKit.ini") == SI_FAIL) {
        Logger::log(NK_ERROR, "Error saving settings file");
    }
}

void PersistedSettings::setValue(const std::string &folder, const std::string &key, const std::string &value) {
    if (ini.SetValue(folder.c_str(), key.c_str(), value.c_str()) == SI_FAIL) {
        Logger::log(NK_ERROR, "Error updating setting");
    }
}
