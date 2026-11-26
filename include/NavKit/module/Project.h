#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../model/CrmdSource.h"
#include "../model/FileSource.h"
#include "ProjectSettings.h"

struct ProjectFile {
    FileSource scene;
    FileSource sceneMesh;
    FileSource navp;
    FileSource airg;
    FileSource gfxf;
    std::vector<CrmdSource> crmds;
    ProjectSettings::SectionValues settings;
};

class Project {
    Project() = default;

public:
    static Project& getInstance() {
        static Project instance;
        return instance;
    }

    static constexpr int FORMAT_VERSION = 1;
    static inline const std::string FILE_EXTENSION = ".nvproj.json";
    static inline const std::string JSON_EXTENSION = ".json";

    std::string projectFile;

    void handleNewProjectClicked();

    void handleOpenProjectClicked();

    void handleSaveProjectClicked();

    bool handleSaveProjectAsClicked();

    void handleExportProjectClicked();

    void newProject();

    bool openProject(const std::string& fileName);

    bool saveProject(const std::string& fileName);

    void exportProject();

    void update();

    static void fileSaved(std::function<void()> recordSavedSource);

    [[nodiscard]] bool isBusy() const;

    [[nodiscard]] static ProjectFile captureCurrentProject();

    [[nodiscard]] static std::string toJson(const ProjectFile& project, const std::filesystem::path& projectDirectory);

    [[nodiscard]] static ProjectFile fromJson(const std::string& json, const std::filesystem::path& projectDirectory);

    [[nodiscard]] static std::string projectName(const std::filesystem::path& projectFilePath);

    [[nodiscard]] static std::string withProjectExtension(const std::string& fileName);

private:
    struct LoadStep {
        std::string label;
        bool needsRpkg;
        std::function<bool()> start;
        std::function<bool()> isBusy;
        std::function<bool()> succeeded;
    };

    struct ExportTask {
        std::string label;
        std::string targetPath;
        std::function<bool()> write;
        std::function<void()> onSuccess;
        bool succeeded = false;
    };

    std::vector<LoadStep> loadSteps;
    size_t currentLoadStep = 0;
    bool currentLoadStepStarted = false;
    std::vector<std::string> loadErrors;

    std::vector<ExportTask> exportTasks;
    std::optional<std::jthread> exportWorker;
    std::atomic_bool exportRunning = false;
    std::atomic_bool exportDone = false;

    static void clearModules();

    static void closeGenerationSettingsDialogs();

    void queueLoadSteps(const ProjectFile& project);

    void advanceLoadStep();

    void finishLoad();

    void finishExport();
};
