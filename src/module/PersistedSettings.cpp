#include "../../include/NavKit/module/PersistedSettings.h"

#include <filesystem>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"

PersistedSettings::PersistedSettings() : ini(CSimpleIniA()) {}

void PersistedSettings::load() {
    ini.SetUnicode();

    const std::string iniFileName = "NavKit.ini";
    const std::string oldIniFileName = "NavKit.ini.old";

    if (std::filesystem::exists(oldIniFileName)) {
        Logger::log(NK_INFO, "Old settings file found. Merging settings...");
        CSimpleIniA oldIni;
        oldIni.SetUnicode();
        if (oldIni.LoadFile(oldIniFileName.c_str()) >= 0) {
            if (ini.LoadFile(iniFileName.c_str()) < 0) {
                Logger::log(NK_ERROR, "New NavKit.ini not found. Creating a new one from old settings.");
            }

            CSimpleIniA::TNamesDepend sections;
            oldIni.GetAllSections(sections);
            // Merge from old settings using the previous settings schema
            if (const auto backgroundColor = oldIni.GetValue("Colors", "backgroundColor", ""); backgroundColor[0] !=
                '\0') {
                ini.SetValue("NavKit", "backgroundColor", backgroundColor);
            }
            if (const auto hitman = oldIni.GetValue("Paths", "hitman", ""); hitman[0] != '\0') {
                ini.SetValue("NavKit", "hitman", hitman);
            }
            if (const auto output = oldIni.GetValue("Paths", "output", ""); output[0] != '\0') {
                ini.SetValue("NavKit", "output", output);
            }
            if (const auto blender = oldIni.GetValue("Paths", "blender", ""); blender[0] != '\0') {
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
                NK_ERROR, "Failed to load NavKit.ini.old for merging. Loading current NavKit.ini if it exists.");
            ini.LoadFile(iniFileName.c_str());
        }

        try {
            std::filesystem::remove(oldIniFileName);
            Logger::log(NK_INFO, "Removed temporary old settings file.");
        } catch (const std::filesystem::filesystem_error& e) {
            Logger::log(NK_ERROR, "Failed to delete NavKit.ini.old: %s", e.what());
        }
    } else {
        ini.LoadFile(iniFileName.c_str());
    }

    if (std::filesystem::exists(iniFileName)) {
        const std::filesystem::path fullPath = std::filesystem::absolute(iniFileName);
        Logger::log(NK_INFO, "Loading settings from %s...", fullPath.string().c_str());
    } else {
        Logger::log(NK_INFO, "No NavKit.ini found, using default settings.");
    }

    NavKitSettings::getInstance().loadSettings();
    Renderer::getInstance().loadSettings();
    Obj::getInstance().loadSettings();
    RecastAdapter::getInstance().loadSettings();
}

void PersistedSettings::save() const {
    if (ini.SaveFile("NavKit.ini") == SI_FAIL) {
        Logger::log(NK_ERROR, "Error saving settings file");
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