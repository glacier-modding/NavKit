#pragma once
#include <SimpleIni.h>

class PersistedSettings {
    explicit PersistedSettings();

    CSimpleIniA ini;

public:
    static PersistedSettings &getInstance() {
        static PersistedSettings instance;
        return instance;
    }

    void setValue(const std::string &folder, const std::string &key, const std::string &value);

    void save() const;

    void load();
};
