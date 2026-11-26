#include "../../include/NavKit/module/ProjectSettings.h"

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/SceneMesh.h"

#include <cstdlib>
#include <optional>

namespace {
    const ProjectSettings::SectionValues DEFAULTS = {
        {"Recast",
            {
                {"cellSize", "0.1"},
                {"cellHeight", "0.1"},
                {"agentHeight", "1.8"},
                {"agentRadius", "0.2"},
                {"agentMaxClimb", "0.41"},
                {"agentMaxSlope", "45"},
                {"regionMinSize", "25"},
                {"regionMergeSize", "30"},
                {"partitionType", "SAMPLE_PARTITION_WATERSHED"},
                {"filterLowHangingObstacles", "1"},
                {"filterLedgeSpans", "1"},
                {"filterWalkableLowHeightSpans", "1"},
                {"polygonizationEdgeMaxLen", "5"},
                {"polygonizationEdgeMaxError", "1.4"},
                {"polygonizationVertsPerPoly", "3"},
                {"detailSampleDist", "1.5"},
                {"detailSampleMaxError", "1.4"},
                {"tilingTileSize", "64"},
            }},
        {"Obj",
            {
                {"meshTypeForBuild", "ALOC"},
                {"primLods", "11111111"},
                {"sceneMeshBuildType", "COPY"},
                {"skipExtractingAlocsOrPrims", "false"},
                {"filterToIncludeBox", "true"},
                {"extractTextures", "false"},
                {"applyTextures", "false"},
            }},
        {"Airg", {{"spacing", "2.25"}}},
        {"Crmd", {{"snapHeightsToGlb", "true"}}},
    };

    const std::string* findValue(
        const ProjectSettings::SectionValues& sections, const std::string& section, const std::string& key) {
        if (const auto sectionIt = sections.find(section); sectionIt != sections.end()) {
            if (const auto valueIt = sectionIt->second.find(key); valueIt != sectionIt->second.end()) {
                return &valueIt->second;
            }
        }
        return nullptr;
    }

    std::optional<double> parseNumber(const std::string& text) {
        if (text.empty()) {
            return std::nullopt;
        }
        char* end = nullptr;
        const double number = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') {
            return std::nullopt;
        }
        return number;
    }

    bool isSameValue(const std::string& left, const std::string& right) {
        if (left == right) {
            return true;
        }
        const std::optional<double> leftNumber = parseNumber(left);
        const std::optional<double> rightNumber = parseNumber(right);
        return leftNumber && rightNumber && static_cast<float>(*leftNumber) == static_cast<float>(*rightNumber);
    }

    bool isDefaultValue(const std::string& section, const std::string& key, const std::string& value) {
        const std::string* defaultValue = findValue(DEFAULTS, section, key);
        return defaultValue && isSameValue(value, *defaultValue);
    }
} // namespace

const char* ProjectSettings::getValue(const std::string& section, const std::string& key) const {
    if (const std::string* value = findValue(values, section, key)) {
        return value->c_str();
    }
    return getDefaultValue(section, key);
}

const char* ProjectSettings::getDefaultValue(const std::string& section, const std::string& key) {
    if (const std::string* defaultValue = findValue(DEFAULTS, section, key)) {
        return defaultValue->c_str();
    }
    Logger::log(NK_ERROR, "No default for project setting [%s] %s.", section.c_str(), key.c_str());
    return "";
}

void ProjectSettings::setValue(const std::string& section, const std::string& key, const std::string& value) {
    if (!isDefaultValue(section, key, value)) {
        values[section][key] = value;
        return;
    }
    if (const auto sectionIt = values.find(section); sectionIt != values.end()) {
        sectionIt->second.erase(key);
        if (sectionIt->second.empty()) {
            values.erase(sectionIt);
        }
    }
}

void ProjectSettings::replaceValues(const SectionValues& newValues) {
    values.clear();
    for (const auto& [section, sectionValues] : newValues) {
        for (const auto& [key, value] : sectionValues) {
            setValue(section, key, value);
        }
    }
}

void ProjectSettings::applyToModules() const {
    RecastAdapter::getInstance().loadSettings();
    SceneMesh::getInstance().loadSettings();
    Crmd::getInstance().loadSettings();
    Grid::getInstance().loadSettings();
}

void ProjectSettings::resetToDefaults() {
    values.clear();
    applyToModules();
}
