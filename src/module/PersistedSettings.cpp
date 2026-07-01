#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../include/NavKit/module/PersistedSettings.h"
#include <filesystem>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/Renderer.h"

PersistedSettings::PersistedSettings() : ini(CSimpleIniA()) {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(buffer).parent_path();

    iniPath = (exeDir / "NavKit.ini").string();
    oldIniPath = (exeDir / "NavKit.ini.old").string();
}

void PersistedSettings::load() {
    ini.SetUnicode();

    if (std::filesystem::exists(oldIniPath)) {
        Logger::log(NK_INFO, "Old settings file found. Merging settings...");
        CSimpleIniA oldIni;
        oldIni.SetUnicode();
        if (oldIni.LoadFile(oldIniPath.c_str()) >= 0) {
            if (ini.LoadFile(iniPath.c_str()) < 0) {
                Logger::log(NK_ERROR, "New NavKit.ini not found. Creating a new one from old settings.");
            }

            CSimpleIniA::TNamesDepend sections;
            oldIni.GetAllSections(sections);

            if (const auto backgroundColor = oldIni.GetValue("NavKit", "backgroundColor", ""); backgroundColor[0] !=
                '\0') {
                ini.SetValue("NavKit", "backgroundColor", backgroundColor);
            }
            if (const auto hitman = oldIni.GetValue("NavKit", "hitman", ""); hitman[0] != '\0') {
                ini.SetValue("NavKit", "hitman", hitman);
            }
            if (const auto output = oldIni.GetValue("NavKit", "output", ""); output[0] != '\0') {
                ini.SetValue("NavKit", "output", output);
            }
            if (const auto blender = oldIni.GetValue("NavKit", "blender", ""); blender[0] != '\0') {
                ini.SetValue("NavKit", "blender", blender);
            }
            // Merge from old settings that match the new settings schema
            for (const auto& section : sections) {
                if (const CSimpleIniA::TKeyVal* keys = oldIni.GetSection(section.pItem)) {
                    for (auto keyval = keys->begin(); keyval != keys->end(); ++keyval) {
                        const auto key = keyval->first.pItem;
                        if (const auto oldValue = oldIni.GetValue(section.pItem, key, "")) {
                            if (oldValue[0] != '\0') {
                                ini.SetValue(section.pItem, key, oldValue);
                            }
                        }
                    }
                }
            }

            Logger::log(NK_INFO, "Settings merged. Saving updated NavKit.ini.");
            save();
        } else {
            Logger::log(
                NK_ERROR, "Failed to load NavKit.ini.old for merging. Loading current settings if they exist.");
            ini.LoadFile(iniPath.c_str());
        }

        try {
            std::filesystem::remove(oldIniPath);
            Logger::log(NK_INFO, "Removed temporary old settings file.");
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::log(NK_ERROR, "Failed to delete NavKit.ini.old: %s", e.what());
        }
    } else {
        ini.LoadFile(iniPath.c_str());
    }

    if (std::filesystem::exists(iniPath)) {
        Logger::log(NK_INFO, "Loading settings from %s...", iniPath.c_str());
    } else {
        Logger::log(NK_INFO, "No NavKit.ini found, using default settings.");
    }

    NavKitSettings::getInstance().loadSettings();
    Renderer::getInstance().loadSettings();
    SceneMesh::getInstance().loadSettings();
    RecastAdapter::getInstance().loadSettings();
}

void PersistedSettings::save() const {
    if (ini.SaveFile(iniPath.c_str()) == SI_FAIL) {
        Logger::log(NK_ERROR, "Error saving settings file to %s", iniPath.c_str());
    }
}

const char* PersistedSettings::getValue(const std::string& folder, const std::string& key,
                                        const std::string& defaultValue) const {
    return ini.GetValue(folder.c_str(), key.c_str(), defaultValue.c_str());
}

void PersistedSettings::setValue(const std::string& folder, const std::string& key, const std::string& value) {
    if (ini.SetValue(folder.c_str(), key.c_str(), value.c_str()) == SI_FAIL) {
        Logger::log(NK_ERROR, "Error updating setting");
    }
}