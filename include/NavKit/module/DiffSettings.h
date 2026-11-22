#pragma once

#include <cstdint>
#include "../util/CrmdDiff.h"

class wxDialog;

class DiffSettings {
public:
    static DiffSettings& getInstance() {
        static DiffSettings instance;
        return instance;
    }

    void loadSettings();
    void saveSettings() const;
    void showDiffSettingsDialog();

    CrmdDiff::Tolerances crmdTolerances;
    uint16_t crmdKinds = CrmdDiff::defaultKinds;

private:
    static wxDialog* hDiffSettingsDialog;
};
