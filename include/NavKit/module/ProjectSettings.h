#pragma once
#include <map>
#include <string>

class ProjectSettings {
    ProjectSettings() = default;

public:
    using SectionValues = std::map<std::string, std::map<std::string, std::string>>;

    static ProjectSettings& getInstance() {
        static ProjectSettings instance;
        return instance;
    }

    [[nodiscard]] const char* getValue(const std::string& section, const std::string& key) const;

    [[nodiscard]] static const char* getDefaultValue(const std::string& section, const std::string& key);

    void setValue(const std::string& section, const std::string& key, const std::string& value);

    [[nodiscard]] const SectionValues& getValues() const {
        return values;
    }

    void replaceValues(const SectionValues& newValues);

    void applyToModules() const;

    void resetToDefaults();

private:
    SectionValues values;
};
