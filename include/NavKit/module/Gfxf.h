#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../render/Model.h"
#include <glm/mat4x4.hpp>
#include <wx/dialog.h>

class Gfxf {
    explicit Gfxf() = default;

    struct ExportResult {
        std::uint64_t generation;
        std::string sourceFile;
        std::vector<std::pair<std::string, std::vector<unsigned char>>> layers;
        std::string error;
    };

    std::mutex resultMutex;
    std::optional<ExportResult> completedLoad;
    std::optional<std::jthread> backgroundWorker;
    std::optional<std::jthread> saveWorker;
    std::optional<std::jthread> buildWorker;
    std::atomic_bool building = false;
    std::mutex gfxDataMutex;
    std::vector<unsigned char> gfxData;
    std::uint64_t loadGeneration = 0;
    Model model;
    void buildGfx();

    void startLoad(const std::string& fileName);

public:
    static Gfxf& getInstance() {
        static Gfxf instance;
        return instance;
    }

    bool gfxfLoaded = false;
    bool showGfxf = true;
    bool loading = false;
    bool saving = false;
    bool gfxBuilt = false;
    float renderScale = 1.0f;
    float renderOpacity = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    int offsetZ = 0;
    std::string loadedFile;
    std::vector<std::string> layerNames;
    size_t selectedLayerIndex = 0;

    static std::map<std::string, std::string> gfxfHashIoiStringMap;
    static std::mutex gfxfHashIoiStringMapMutex;
    static wxDialog* hGfxfDialog;
    static wxDialog* hGfxfSettingsDialog;

    [[nodiscard]] bool canLoad() const;
    [[nodiscard]] bool canSave() const;

    void handleOpenGfxfClicked();

    void loadGfxfFromFile(const std::string& fileName);
    void exportGfxfToGfxfFile(const std::string& fileName);
    void setGfxData(std::vector<unsigned char> bytes);

    static std::vector<unsigned char> extractGfxFromGfxf(const std::vector<unsigned char>& gfxfBytes);
    static std::vector<unsigned char> loadGfxFromFile(const std::string& fileName);
    static bool packGfxToGfxfFile(const std::vector<unsigned char>& gfxBytes, const std::string& fileName);

    static char* openSaveGfxfFileDialog();
    void handleSaveGfxfClicked();

    void showExtractGfxfDialog();
    void updateDialogControls(wxDialog* dialog);

    void showGfxfSettingsDialog();

    void resetDefaults();

    void finalizeLoad();

    void render(const class Shader& shader, const glm::mat4& view, const glm::mat4& projection) const;

    void clear();

    void startGfxBuild();
};
