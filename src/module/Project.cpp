#include "../../include/NavKit/module/Project.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <wx/app.h>
#include <wx/msgdlg.h>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/model/Json.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Crmd.h"
#include "../../include/NavKit/module/Gfxf.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/NavKit/util/FileUtil.h"

namespace {
    const nfdu8filteritem_t PROJECT_FILE_FILTER = {"NavKit project files", "nvproj.json"};

    std::string sourceKindName(const FileSource::Kind kind) {
        if (kind == FileSource::Kind::Disk) {
            return "disk";
        }
        if (kind == FileSource::Kind::Rpkg) {
            return "rpkg";
        }
        return "generated";
    }

    std::string toStoredPath(const std::string& path, const std::filesystem::path& projectDirectory) {
        const std::filesystem::path absolutePath =
            std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
        const std::filesystem::path relativePath = absolutePath.lexically_relative(projectDirectory.lexically_normal());
        if (!relativePath.empty() && *relativePath.begin() != "..") {
            return relativePath.generic_string();
        }
        return absolutePath.generic_string();
    }

    std::string fromStoredPath(const std::string& storedPath, const std::filesystem::path& projectDirectory) {
        const std::filesystem::path path = std::filesystem::u8path(storedPath);
        return (path.is_absolute() ? path : projectDirectory / path).lexically_normal().string();
    }

    void writeSource(std::ostream& out, const FileSource& source, const std::filesystem::path& projectDirectory,
        const std::string& crowdKey = {}) {
        out << R"({"source":")" << sourceKindName(source.kind) << '"';
        if (source.kind == FileSource::Kind::Disk) {
            out << R"(,"path":")" << Json::escaped(toStoredPath(source.path, projectDirectory)) << '"';
        } else if (source.kind == FileSource::Kind::Rpkg) {
            out << R"(,"hash":")" << Json::escaped(source.hash) << '"';
        }
        if (!crowdKey.empty()) {
            out << R"(,"crowd":")" << Json::escaped(crowdKey) << '"';
        }
        out << '}';
    }

    FileSource toFileSource(const std::string& kind, const std::string& path, const std::string& hash,
        const std::filesystem::path& projectDirectory) {
        if (kind == "disk") {
            if (path.empty()) {
                throw std::runtime_error("A disk source is missing its path.");
            }
            return FileSource::disk(fromStoredPath(path, projectDirectory));
        }
        if (kind == "rpkg") {
            if (hash.empty()) {
                throw std::runtime_error("An rpkg source is missing its hash.");
            }
            return FileSource::rpkg(hash);
        }
        if (kind == "generated") {
            return FileSource::generated();
        }
        throw std::runtime_error("Unknown source type: '" + kind + "'.");
    }

    CrmdSource readCrmdSource(simdjson::ondemand::value value, const std::filesystem::path& projectDirectory) {
        std::string kind;
        std::string path;
        std::string hash;
        std::string crowdKey;
        for (auto field : value.get_object()) {
            const std::string_view key = field.unescaped_key().value();
            if (key == "source") {
                kind = std::string(field.value().get_string().value());
            } else if (key == "path") {
                path = std::string(field.value().get_string().value());
            } else if (key == "hash") {
                hash = std::string(field.value().get_string().value());
            } else if (key == "crowd") {
                crowdKey = std::string(field.value().get_string().value());
            }
        }
        return {toFileSource(kind, path, hash, projectDirectory), crowdKey};
    }

    FileSource readSource(simdjson::ondemand::value value, const std::filesystem::path& projectDirectory) {
        return readCrmdSource(value, projectDirectory).file;
    }

    std::string sanitizeFileName(std::string name) {
        if (const size_t extension = name.find_last_of('.'); extension != std::string::npos && extension > 0) {
            name.erase(extension);
        }
        std::ranges::replace_if(
            name, [](const char c) { return std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos; }, '_');
        return name.empty() ? "crowd" : name;
    }

    bool fileExists(const std::string& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(std::filesystem::u8path(path), error);
    }

    bool removeExisting(const std::string& path) {
        std::error_code error;
        std::filesystem::remove(std::filesystem::u8path(path), error);
        return !error;
    }

    bool copyFileTo(const std::string& from, const std::string& to) {
        std::error_code error;
        if (std::filesystem::equivalent(std::filesystem::u8path(from), std::filesystem::u8path(to), error)) {
            return true;
        }
        std::filesystem::copy_file(std::filesystem::u8path(from), std::filesystem::u8path(to),
            std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            Logger::log(NK_ERROR, "Could not copy '%s' to '%s': %s", from.c_str(), to.c_str(), error.message().c_str());
            return false;
        }
        return true;
    }

    void showMessage(const std::string& message, const std::string& title, const long style) {
        if (wxTheApp) {
            wxMessageBox(wxString::FromUTF8(message), wxString::FromUTF8(title), wxOK | style, getMainFrame());
        }
    }
} // namespace

void Project::clearModules() {
    Scene::getInstance().clear();
    SceneMesh::getInstance().clear();
    Gfxf::getInstance().clear();
    Navp::getInstance().clear();
    Airg::getInstance().clear();
    Crmd::getInstance().clear();
}

void Project::closeGenerationSettingsDialogs() {
    for (wxDialog* dialog :
        {RecastAdapter::hRecastDialog, SceneMesh::hSceneMeshDialog, Airg::hAirgDialog, Crmd::hCrmdSettingsDialog}) {
        if (dialog) {
            dialog->Close();
        }
    }
}

void Project::newProject() {
    clearModules();
    closeGenerationSettingsDialogs();
    ProjectSettings::getInstance().resetToDefaults();
    projectFile.clear();
    loadSteps.clear();
    loadErrors.clear();
    Menu::updateMenuState();
}

void Project::handleNewProjectClicked() {
    if (isBusy()) {
        return;
    }
    newProject();
    Logger::log(NK_INFO, "Started a new project.");
}

void Project::handleOpenProjectClicked() {
    if (isBusy()) {
        return;
    }
    if (char* fileName = FileUtil::openNfdLoadDialog(&PROJECT_FILE_FILTER, 1)) {
        const std::string path = fileName;
        NFD_FreePathU8(fileName);
        openProject(path);
    }
}

void Project::handleSaveProjectClicked() {
    if (isBusy()) {
        return;
    }
    if (projectFile.empty()) {
        handleSaveProjectAsClicked();
        return;
    }
    saveProject(projectFile);
}

bool Project::handleSaveProjectAsClicked() {
    if (isBusy()) {
        return false;
    }
    const std::string defaultName =
        projectFile.empty() ? "project" + FILE_EXTENSION : std::filesystem::u8path(projectFile).filename().string();
    char* fileName = FileUtil::openNfdSaveDialog(&PROJECT_FILE_FILTER, 1, defaultName.c_str());
    if (!fileName) {
        return false;
    }
    const std::string path = fileName;
    NFD_FreePathU8(fileName);
    return saveProject(withProjectExtension(path));
}

void Project::handleExportProjectClicked() {
    if (isBusy()) {
        return;
    }
    if (projectFile.empty() && !handleSaveProjectAsClicked()) {
        return;
    }
    exportProject();
}

std::string Project::withProjectExtension(const std::string& fileName) {
    if (fileName.size() >= FILE_EXTENSION.size() &&
        fileName.compare(fileName.size() - FILE_EXTENSION.size(), FILE_EXTENSION.size(), FILE_EXTENSION) == 0) {
        return fileName;
    }
    std::string base = fileName;
    if (base.size() >= JSON_EXTENSION.size() &&
        base.compare(base.size() - JSON_EXTENSION.size(), JSON_EXTENSION.size(), JSON_EXTENSION) == 0) {
        base.erase(base.size() - JSON_EXTENSION.size());
    }
    return base + FILE_EXTENSION;
}

std::string Project::projectName(const std::filesystem::path& projectFilePath) {
    std::string name = projectFilePath.filename().string();
    if (name.size() > FILE_EXTENSION.size() &&
        name.compare(name.size() - FILE_EXTENSION.size(), FILE_EXTENSION.size(), FILE_EXTENSION) == 0) {
        name.erase(name.size() - FILE_EXTENSION.size());
    } else {
        name = projectFilePath.stem().string();
    }
    return name.empty() ? "project" : name;
}

ProjectFile Project::captureCurrentProject() {
    const auto loadedSource = [](const bool loaded, const FileSource& source) {
        if (!loaded) {
            return FileSource{};
        }
        return source.kind == FileSource::Kind::None ? FileSource::generated() : source;
    };
    ProjectFile project;
    project.scene = loadedSource(Scene::getInstance().sceneLoaded, Scene::getInstance().source);
    project.sceneMesh = loadedSource(SceneMesh::getInstance().glbLoaded, SceneMesh::getInstance().source);
    project.navp = loadedSource(Navp::getInstance().navpLoaded, Navp::getInstance().source);
    project.airg = loadedSource(Airg::getInstance().airgLoaded, Airg::getInstance().source);
    project.gfxf = loadedSource(Gfxf::getInstance().gfxfLoaded, Gfxf::getInstance().source);
    for (const auto& [file, crowdKey] : Crmd::getInstance().crowdMapSources()) {
        project.crmds.push_back({loadedSource(true, file), crowdKey});
    }
    project.settings = ProjectSettings::getInstance().getValues();
    return project;
}

std::string Project::toJson(const ProjectFile& project, const std::filesystem::path& projectDirectory) {
    std::ostringstream out;
    out << R"({"version":)" << FORMAT_VERSION << R"(,"files":{)";
    const char* separator = "";
    const auto writeNamedSource = [&](const char* name, const FileSource& source) {
        if (source.kind == FileSource::Kind::None) {
            return;
        }
        out << separator << '"' << name << "\":";
        writeSource(out, source, projectDirectory);
        separator = ",";
    };
    writeNamedSource("scene", project.scene);
    writeNamedSource("sceneMesh", project.sceneMesh);
    writeNamedSource("navp", project.navp);
    writeNamedSource("airg", project.airg);
    writeNamedSource("gfxf", project.gfxf);
    if (!project.crmds.empty()) {
        out << separator << R"("crmds":[)";
        const char* crmdSeparator = "";
        for (const auto& [file, crowdKey] : project.crmds) {
            out << crmdSeparator;
            writeSource(out, file, projectDirectory, crowdKey);
            crmdSeparator = ",";
        }
        out << ']';
    }
    out << R"(},"settings":{)";
    const char* sectionSeparator = "";
    for (const auto& [section, values] : project.settings) {
        out << sectionSeparator << '"' << Json::escaped(section) << "\":{";
        const char* valueSeparator = "";
        for (const auto& [key, value] : values) {
            out << valueSeparator << '"' << Json::escaped(key) << R"(":")" << Json::escaped(value) << '"';
            valueSeparator = ",";
        }
        out << '}';
        sectionSeparator = ",";
    }
    out << "}}";
    return out.str();
}

ProjectFile Project::fromJson(const std::string& json, const std::filesystem::path& projectDirectory) {
    ProjectFile project;
    const simdjson::padded_string input(json);
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document document = parser.iterate(input);
    for (auto field : document.get_object()) {
        const std::string_view key = field.unescaped_key().value();
        if (key == "version") {
            if (const int64_t version = field.value().get_int64().value(); version > FORMAT_VERSION) {
                Logger::log(NK_WARN, "Project file version %lld is newer than this NavKit supports (%d).",
                    static_cast<long long>(version), FORMAT_VERSION);
            }
        } else if (key == "files") {
            for (auto fileField : field.value().get_object()) {
                const std::string_view fileKey = fileField.unescaped_key().value();
                if (fileKey == "scene") {
                    project.scene = readSource(fileField.value(), projectDirectory);
                } else if (fileKey == "sceneMesh") {
                    project.sceneMesh = readSource(fileField.value(), projectDirectory);
                } else if (fileKey == "navp") {
                    project.navp = readSource(fileField.value(), projectDirectory);
                } else if (fileKey == "airg") {
                    project.airg = readSource(fileField.value(), projectDirectory);
                } else if (fileKey == "gfxf") {
                    project.gfxf = readSource(fileField.value(), projectDirectory);
                } else if (fileKey == "crmds") {
                    for (auto crmdResult : fileField.value().get_array()) {
                        project.crmds.push_back(readCrmdSource(crmdResult.value(), projectDirectory));
                    }
                }
            }
        } else if (key == "settings") {
            for (auto sectionField : field.value().get_object()) {
                const std::string section(sectionField.unescaped_key().value());
                for (auto valueField : sectionField.value().get_object()) {
                    const std::string valueKey(valueField.unescaped_key().value());
                    project.settings[section][valueKey] = std::string(valueField.value().get_string().value());
                }
            }
        }
    }
    return project;
}

bool Project::saveProject(const std::string& fileName) {
    const std::filesystem::path path = std::filesystem::absolute(std::filesystem::u8path(fileName));
    const std::string json = toJson(captureCurrentProject(), path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << json;
    out.close();
    if (!out) {
        const std::string message = "Could not write project file: " + path.string();
        Logger::log(NK_ERROR, "%s", message.c_str());
        showMessage(message, "Project save failed", wxICON_ERROR);
        return false;
    }
    projectFile = path.string();
    Logger::log(NK_INFO, "Saved project to '%s'.", projectFile.c_str());
    Menu::updateMenuState();
    return true;
}

void Project::fileSaved(std::function<void()> recordSavedSource) {
    auto apply = [recordSavedSource = std::move(recordSavedSource)] {
        recordSavedSource();
        Project& project = getInstance();
        if (!project.projectFile.empty() && !project.isBusy()) {
            project.saveProject(project.projectFile);
        }
    };
    if (wxTheApp) {
        wxTheApp->CallAfter(std::move(apply));
    } else {
        apply();
    }
}

bool Project::openProject(const std::string& fileName) {
    const std::filesystem::path path = std::filesystem::absolute(std::filesystem::u8path(fileName));
    ProjectFile project;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("Could not open the file.");
        }
        std::stringstream contents;
        contents << in.rdbuf();
        project = fromJson(contents.str(), path.parent_path());
    } catch (const std::exception& error) {
        const std::string message = "Could not read project file '" + path.string() + "': " + error.what();
        Logger::log(NK_ERROR, "%s", message.c_str());
        showMessage(message, "Project load failed", wxICON_ERROR);
        return false;
    }

    Logger::log(NK_INFO, "Opening project '%s'...", path.string().c_str());
    clearModules();
    closeGenerationSettingsDialogs();
    ProjectSettings& projectSettings = ProjectSettings::getInstance();
    projectSettings.replaceValues(project.settings);
    projectSettings.applyToModules();
    projectFile = path.string();
    queueLoadSteps(project);
    Menu::updateMenuState();
    return true;
}

void Project::queueLoadSteps(const ProjectFile& project) {
    loadSteps.clear();
    loadErrors.clear();
    currentLoadStep = 0;
    currentLoadStepStarted = false;

    const auto addStep = [this](const std::string& label, const FileSource& source,
                             const std::function<bool(const std::string&)>& startFromDisk,
                             const std::function<bool(const std::string&)>& startFromRpkg,
                             const std::function<bool()>& isBusy, const std::function<bool()>& succeeded) {
        switch (source.kind) {
        case FileSource::Kind::None:
            return;
        case FileSource::Kind::Generated:
            loadErrors.push_back(label + " was generated but never exported, so it was not loaded.");
            return;
        case FileSource::Kind::Disk:
            loadSteps.push_back({label + " (" + source.path + ")", false,
                [this, label, path = source.path, startFromDisk] {
                    if (!fileExists(path)) {
                        loadErrors.push_back(label + " file not found: " + path);
                        return false;
                    }
                    return startFromDisk(path);
                },
                isBusy, succeeded});
            return;
        case FileSource::Kind::Rpkg:
            loadSteps.push_back({label + " (rpkg " + source.hash + ")", true,
                [hash = source.hash, startFromRpkg] { return startFromRpkg(hash); }, isBusy, succeeded});
        }
    };
    const auto unsupported = [](const std::string&) { return false; };

    Scene& scene = Scene::getInstance();
    addStep(
        "Scene", project.scene,
        [&scene](const std::string& path) {
            scene.loadSceneFromFile(path, FileSource::disk(path));
            return true;
        },
        unsupported, [&scene] { return scene.loading.load(); }, [&scene] { return scene.sceneLoaded; });

    SceneMesh& sceneMesh = SceneMesh::getInstance();
    addStep(
        "GLB", project.sceneMesh,
        [&sceneMesh](const std::string& path) {
            sceneMesh.loadGlbFromFile(path, FileSource::disk(path));
            return true;
        },
        unsupported, [&sceneMesh] { return sceneMesh.isLoadingGlb(); }, [&sceneMesh] { return sceneMesh.glbLoaded; });

    Navp& navp = Navp::getInstance();
    addStep(
        "Navp", project.navp,
        [&navp](const std::string& path) {
            navp.loadedNavpText = path;
            return navp.loadNavpFromFile(path);
        },
        [&navp](const std::string& hash) {
            navp.loadedNavpText =
                Rpkg::displayNameForHash(Navp::navpHashIoiStringMap, Navp::navpHashIoiStringMapMutex, hash);
            return Navp::extractNavpFromRpkgs(hash);
        },
        [&navp] { return navp.loading; }, [&navp] { return navp.navpLoaded; });

    Airg& airg = Airg::getInstance();
    addStep(
        "Airg", project.airg,
        [&airg](const std::string& path) {
            airg.loadedAirgText = path;
            return airg.loadAirgFromFile(path);
        },
        [&airg](const std::string& hash) {
            airg.loadedAirgText =
                Rpkg::displayNameForHash(Airg::airgHashIoiStringMap, Airg::airgHashIoiStringMapMutex, hash);
            return Airg::extractAirgFromRpkgs(hash);
        },
        [&airg] { return airg.airgLoading; }, [&airg] { return airg.airgLoaded; });

    Crmd& crmd = Crmd::getInstance();
    for (const CrmdSource& source : project.crmds) {
        auto countBeforeLoad = std::make_shared<size_t>(0);
        addStep(
            "Crmd", source.file,
            [&crmd, countBeforeLoad, crowdKey = source.crowdKey](const std::string& path) {
                *countBeforeLoad = crmd.crowdMapCount();
                return crmd.loadCrmdFromFiles(
                    {{path, std::filesystem::u8path(path).filename().string(), FileSource::disk(path), crowdKey}});
            },
            [&crmd, countBeforeLoad, crowdKey = source.crowdKey](const std::string& hash) {
                *countBeforeLoad = crmd.crowdMapCount();
                return Crmd::extractCrmdsFromRpkgs(
                    {{hash, Rpkg::displayNameForHash(Crmd::crmdHashIoiStringMap, Crmd::crmdHashIoiStringMapMutex, hash),
                        crowdKey}});
            },
            [&crmd] { return crmd.loading.load(); },
            [&crmd, countBeforeLoad] { return crmd.crowdMapCount() > *countBeforeLoad; });
    }

    Gfxf& gfxf = Gfxf::getInstance();
    addStep(
        "Gfxf", project.gfxf, [&gfxf](const std::string& path) { return gfxf.loadGfxfFromFile(path); },
        [](const std::string& hash) { return Gfxf::extractGfxfFromRpkgs(hash); }, [&gfxf] { return gfxf.loading; },
        [&gfxf] { return gfxf.gfxfLoaded; });

    if (loadSteps.empty()) {
        finishLoad();
    }
}

void Project::advanceLoadStep() {
    ++currentLoadStep;
    currentLoadStepStarted = false;
    if (currentLoadStep >= loadSteps.size()) {
        finishLoad();
    }
}

void Project::finishLoad() {
    loadSteps.clear();
    currentLoadStep = 0;
    currentLoadStepStarted = false;
    Menu::updateMenuState();
    if (loadErrors.empty()) {
        Logger::log(NK_INFO, "Finished opening project '%s'.", projectFile.c_str());
        return;
    }
    std::string message = "The project opened, but some files did not load:\n";
    for (const std::string& error : loadErrors) {
        Logger::log(NK_WARN, "%s", error.c_str());
        message += "\n- " + error;
    }
    loadErrors.clear();
    showMessage(message, "Project opened with problems", wxICON_WARNING);
}

void Project::exportProject() {
    if (projectFile.empty() || isBusy()) {
        return;
    }
    const std::filesystem::path projectPath = std::filesystem::u8path(projectFile);
    const std::filesystem::path directory = projectPath.parent_path();
    const std::string name = projectName(projectPath);
    const auto target = [&directory](const std::string& fileName) { return (directory / fileName).string(); };
    exportTasks.clear();

    Scene& scene = Scene::getInstance();
    if (scene.sceneLoaded && scene.source.kind != FileSource::Kind::Rpkg) {
        const std::string path = target(name + ".nav.json");
        exportTasks.push_back({"Scene", path,
            [&scene, path] {
                if (!removeExisting(path)) {
                    return false;
                }
                scene.saveScene(path);
                return fileExists(path);
            },
            [&scene, path] { scene.source = FileSource::disk(path); }});
    }

    SceneMesh& sceneMesh = SceneMesh::getInstance();
    if (sceneMesh.glbLoaded && sceneMesh.source.kind != FileSource::Kind::Rpkg) {
        const std::string path = target(name + ".glb");
        exportTasks.push_back({"GLB", path, [from = sceneMesh.lastGlbFileName, path] { return copyFileTo(from, path); },
            [&sceneMesh, path] { sceneMesh.source = FileSource::disk(path); }});
    }
    if (sceneMesh.blendFileBuilt) {
        const std::string from =
            (std::filesystem::u8path(NavKitSettings::getInstance().outputFolder) / "output.blend").string();
        const std::string path = target(name + ".blend");
        exportTasks.push_back({"Blend", path, [from, path] { return copyFileTo(from, path); }, [] {}});
    }

    Navp& navp = Navp::getInstance();
    if (navp.navpLoaded && navp.source.kind != FileSource::Kind::Rpkg) {
        const std::string path = target(name + ".NAVP");
        exportTasks.push_back({"Navp", path,
            [&navp, path] {
                if (!removeExisting(path)) {
                    return false;
                }
                navp.saveNavMesh(path, "NAVP");
                return fileExists(path);
            },
            [&navp, path] { navp.source = FileSource::disk(path); }});
    }

    Airg& airg = Airg::getInstance();
    if (airg.airgLoaded && airg.source.kind != FileSource::Kind::Rpkg) {
        const std::string path = target(name + ".AIRG");
        exportTasks.push_back({"Airg", path,
            [&airg, path] {
                if (!removeExisting(path)) {
                    return false;
                }
                Airg::saveAirg(&airg, path);
                return fileExists(path);
            },
            [&airg, path] { airg.source = FileSource::disk(path); }});
    }

    Crmd& crmd = Crmd::getInstance();
    const std::vector<std::string> crowdMapNames = crmd.crowdMapNames();
    const std::vector<CrmdSource> crowdMapSources = crmd.crowdMapSources();
    std::set<std::string> usedCrmdNames;
    for (size_t index = 0; index < crowdMapSources.size() && index < crowdMapNames.size(); ++index) {
        if (crowdMapSources[index].file.kind == FileSource::Kind::Rpkg) {
            continue;
        }
        const std::string baseName = sanitizeFileName(crowdMapNames[index]);
        std::string fileName = baseName;
        for (int suffix = 2; usedCrmdNames.contains(fileName); ++suffix) {
            fileName = baseName + "_" + std::to_string(suffix);
        }
        usedCrmdNames.insert(fileName);
        const std::string path = target(fileName + ".CRMD");
        exportTasks.push_back({"Crmd " + crowdMapNames[index], path,
            [&crmd, path, index] {
                try {
                    crmd.saveCrmd(path, index);
                    return true;
                } catch (const std::exception& error) {
                    Logger::log(NK_ERROR, "Failed to export Crmd to '%s': %s", path.c_str(), error.what());
                    return false;
                }
            },
            [&crmd, path, index] { crmd.setCrowdMapSource(index, FileSource::disk(path)); }});
    }

    Gfxf& gfxf = Gfxf::getInstance();
    if (gfxf.gfxfLoaded && gfxf.source.kind != FileSource::Kind::Rpkg) {
        if (!gfxf.canSave()) {
            Logger::log(NK_WARN, "The loaded map has no GFX data to export (SWF files cannot be saved), skipping it.");
        } else {
            std::string extension = std::filesystem::u8path(gfxf.loadedFile).extension().string();
            std::ranges::transform(extension, extension.begin(), ::tolower);
            const bool asGfx = extension == ".gfx" && gfxf.source.kind == FileSource::Kind::Disk;
            const std::string path = target(name + (asGfx ? ".gfx" : ".GFXF"));
            exportTasks.push_back({"Gfxf", path,
                [&gfxf, path, asGfx] {
                    if (!removeExisting(path)) {
                        return false;
                    }
                    if (asGfx) {
                        gfxf.exportGfxToGfxFile(path);
                    } else {
                        gfxf.exportGfxfToGfxfFile(path);
                    }
                    return fileExists(path);
                },
                [&gfxf, path] { gfxf.source = FileSource::disk(path); }});
        }
    }

    Logger::log(NK_INFO, "Exporting project to '%s'...", directory.string().c_str());
    exportRunning = true;
    exportDone = false;
    Menu::updateMenuState();
    exportWorker.emplace([this] {
        for (ExportTask& task : exportTasks) {
            Logger::log(NK_INFO, "Exporting %s to '%s'...", task.label.c_str(), task.targetPath.c_str());
            try {
                task.succeeded = task.write();
            } catch (const std::exception& error) {
                Logger::log(NK_ERROR, "Exporting %s failed: %s", task.label.c_str(), error.what());
                task.succeeded = false;
            }
        }
        exportDone = true;
    });
}

void Project::finishExport() {
    exportWorker.reset();
    exportDone = false;
    std::vector<std::string> failures;
    for (const ExportTask& task : exportTasks) {
        if (task.succeeded) {
            task.onSuccess();
        } else {
            failures.push_back(task.label + " (" + task.targetPath + ")");
        }
    }
    const size_t exportedCount = exportTasks.size() - failures.size();
    exportTasks.clear();
    exportRunning = false;
    const bool projectSaved = saveProject(projectFile);
    Logger::log(NK_INFO, "Exported %zu file(s) for project '%s'.", exportedCount, projectFile.c_str());
    if (!failures.empty()) {
        std::string message = "Some files could not be exported. See the log for details.\n";
        for (const std::string& failure : failures) {
            message += "\n- " + failure;
        }
        if (!projectSaved) {
            message += "\n\nThe project file could not be saved either.";
        }
        showMessage(message, "Project export failed", wxICON_ERROR);
    }
    Menu::updateMenuState();
}

void Project::update() {
    if (exportDone) {
        finishExport();
    }
    if (currentLoadStep >= loadSteps.size()) {
        return;
    }
    LoadStep& step = loadSteps[currentLoadStep];
    if (!currentLoadStepStarted) {
        if (step.needsRpkg && !Rpkg::canExtract()) {
            if (Rpkg::extractionDataInitInProgress) {
                return;
            }
            loadErrors.push_back(step.label +
                ": resource packages are not available. Check the Hitman folder in "
                "NavKit Settings.");
            advanceLoadStep();
            return;
        }
        Logger::log(NK_INFO, "Project: loading %s...", step.label.c_str());
        if (const size_t errorCount = loadErrors.size(); !step.start()) {
            if (loadErrors.size() == errorCount) {
                loadErrors.push_back("Could not start loading " + step.label + ".");
            }
            advanceLoadStep();
            return;
        }
        currentLoadStepStarted = true;
        return;
    }
    if (step.isBusy()) {
        return;
    }
    if (!step.succeeded()) {
        loadErrors.push_back(step.label + " failed to load. See the log for details.");
    }
    advanceLoadStep();
}

bool Project::isBusy() const {
    return exportRunning || currentLoadStep < loadSteps.size();
}
