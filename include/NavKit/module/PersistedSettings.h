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

    const char *getValue(const std::string &folder, const std::string &key, const std::string &defaultValue) const;

    void setValue(const std::string &folder, const std::string &key, const std::string &value);

    void save() const;

    void load();
};
