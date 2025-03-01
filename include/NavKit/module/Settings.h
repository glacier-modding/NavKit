#pragma once
#include <SimpleIni.h>

class Settings {
    explicit Settings() : ini(CSimpleIniA()) {
    }

    CSimpleIniA ini;

public:
    static Settings &getInstance() {
        static Settings instance;
        return instance;
    }

    static void setValue(const std::string& folder, const std::string& key, const std::string& value);
    static void save();
    static void Load();
};
