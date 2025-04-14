#pragma once
#include <SimpleIni.h>

class Settings {
    explicit Settings();

    CSimpleIniA ini;
    int settingsScroll;

public:
    static Settings &getInstance() {
        static Settings instance;
        return instance;
    }

    float backgroundColor;

    static void setValue(const std::string &folder, const std::string &key, const std::string &value);

    static void save();

    static void Load();

    static const int SETTINGS_MENU_HEIGHT;

    void drawMenu();
};
