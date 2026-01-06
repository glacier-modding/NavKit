#include "../../include/NavKit/module/Obj.h"

#include <direct.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/glacier2obj/glacier2obj.h"
#include "../../include/NavWeakness/NavPower.h"

HWND Obj::hObjDialog = nullptr;

Obj::Obj() : loadObjName("Load Obj"),
             saveObjName("Save Obj"),
             lastObjFileName("Load Obj"),
             lastSaveObjFileName("Save Obj"),
             objLoaded(false),
             showObj(true),
             loadObj(false),
             startedObjGeneration(false),
             blenderObjStarted(false),
             blenderObjGenerationDone(false),
             blendFileOnlyBuild(false),
             blendFileAndObjBuild(false),
             filterToIncludeBox(true),
             glacier2ObjDebugLogsEnabled(false),
             errorBuilding(false),
             skipExtractingAlocsOrPrims(false),
             errorExtracting(false),
             extractingAlocsOrPrims(false),
             doneExtractingAlocsOrPrims(false),
             doObjHitTest(false),
             meshTypeForBuild(ALOC),
             sceneMeshBuildType(COPY),
             primLods{true, true, true, true, true, true, true, true},
             blendFileBuilt(false) {
}

void Obj::updateObjDialogControls(HWND hDlg) {
    Obj &obj = getInstance();
    CheckRadioButton(hDlg, IDC_RADIO_MESH_TYPE_ALOC, IDC_RADIO_MESH_TYPE_PRIM,
                     obj.meshTypeForBuild == ALOC ? IDC_RADIO_MESH_TYPE_ALOC : IDC_RADIO_MESH_TYPE_PRIM);

    for (int i = 0; i < 8; ++i) {
        CheckDlgButton(hDlg, IDC_CHECK_PRIM_LOD_1 + i, obj.primLods[i] ? BST_CHECKED : BST_UNCHECKED);
    }
    bool isPrim = obj.meshTypeForBuild == PRIM;
    for (int i = 0; i < 8; ++i) {
        EnableWindow(GetDlgItem(hDlg, IDC_CHECK_PRIM_LOD_1 + i), isPrim);
    }
    CheckRadioButton(hDlg, IDC_RADIO_BUILD_TYPE_COPY, IDC_RADIO_BUILD_TYPE_INSTANCE,
                     obj.sceneMeshBuildType == COPY ? IDC_RADIO_BUILD_TYPE_COPY : IDC_RADIO_BUILD_TYPE_INSTANCE);

    CheckDlgButton(hDlg, IDC_CHECK_SKIP_RPKG_EXTRACT, obj.skipExtractingAlocsOrPrims ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_FILTER_TO_INCLUDE_BOX, obj.filterToIncludeBox ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_CHECK_SHOW_BLENDER_DEBUG_LOGS, obj.glacier2ObjDebugLogsEnabled ? BST_CHECKED : BST_UNCHECKED);
}

INT_PTR CALLBACK Obj::ObjSettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    Obj &obj = getInstance();
    switch (message) {
        case WM_INITDIALOG: {
            updateObjDialogControls(hDlg);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_RADIO_MESH_TYPE_ALOC:
                case IDC_RADIO_MESH_TYPE_PRIM: {
                    obj.meshTypeForBuild = IsDlgButtonChecked(hDlg, IDC_RADIO_MESH_TYPE_ALOC) ? ALOC : PRIM;
                    obj.saveObjSettings();
                    Logger::log(NK_INFO, "Mesh type for build set to %s.",
                                obj.meshTypeForBuild == ALOC ? "Aloc" : "Prim");
                    updateObjDialogControls(hDlg);
                    return TRUE;
                }
                case IDC_RADIO_BUILD_TYPE_COPY:
                case IDC_RADIO_BUILD_TYPE_INSTANCE: {
                        obj.sceneMeshBuildType = IsDlgButtonChecked(hDlg, IDC_RADIO_BUILD_TYPE_COPY) ? COPY : INSTANCE;
                        obj.saveObjSettings();
                        Logger::log(NK_INFO, "Scene Mesh Build type set to %s.",
                                    obj.sceneMeshBuildType == COPY ? "Copy" : "Instance");
                        updateObjDialogControls(hDlg);
                        return TRUE;
                }

                case IDC_CHECK_FILTER_TO_INCLUDE_BOX: {
                        obj.filterToIncludeBox = IsDlgButtonChecked(hDlg, IDC_CHECK_FILTER_TO_INCLUDE_BOX) ? COPY : INSTANCE;
                        obj.saveObjSettings();
                        Logger::log(NK_INFO, "Filter to include box set to %s.", obj.filterToIncludeBox ? "true" : "false");
                        updateObjDialogControls(hDlg);
                        return TRUE;
                    }

                case IDC_CHECK_SKIP_RPKG_EXTRACT: {
                        obj.skipExtractingAlocsOrPrims = IsDlgButtonChecked(hDlg, IDC_CHECK_SKIP_RPKG_EXTRACT) ? COPY : INSTANCE;
                        obj.saveObjSettings();
                        Logger::log(NK_INFO, "Skip Extracting ALOCs or PRIMs set to %s.", obj.skipExtractingAlocsOrPrims ? "true" : "false");
                        updateObjDialogControls(hDlg);
                        return TRUE;
                    }
                case IDC_CHECK_SHOW_BLENDER_DEBUG_LOGS: {
                        obj.glacier2ObjDebugLogsEnabled = IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_BLENDER_DEBUG_LOGS) ? COPY : INSTANCE;
                        obj.saveObjSettings();
                        Logger::log(NK_INFO, "Show Blender Debug Logs set to %s.", obj.glacier2ObjDebugLogsEnabled ? "true" : "false");
                        updateObjDialogControls(hDlg);
                        return (INT_PTR) TRUE;
                    }

                case IDC_BUTTON_RESET_DEFAULTS: {
                    obj.resetDefaults();
                    updateObjDialogControls(hDlg);
                    Logger::log(NK_INFO, "Prim LODs set to %s.", obj.buildPrimLodsString().c_str());
                    Logger::log(NK_INFO, "Mesh type for build set to %s.",
                    obj.meshTypeForBuild == ALOC ? "Aloc" : "Prim");
                    Logger::log(NK_INFO, "Scene Mesh Build type set to %s.",
                                obj.sceneMeshBuildType == COPY ? "Copy" : "Instance");
                    obj.saveObjSettings();
                    break;
                    }
                case WM_CLOSE:
                    DestroyWindow(hDlg);
                    return TRUE;

                default:
                    if (LOWORD(wParam) >= IDC_CHECK_PRIM_LOD_1 && LOWORD(wParam) <= IDC_CHECK_PRIM_LOD_8) {
                        int index = LOWORD(wParam) - IDC_CHECK_PRIM_LOD_1;
                        bool isChecked = IsDlgButtonChecked(hDlg, LOWORD(wParam)) == BST_CHECKED;
                        obj.primLods[index] = isChecked;
                        obj.saveObjSettings();
                        Logger::log(NK_INFO, "Prim LODs set to %s.", obj.buildPrimLodsString().c_str());
                    }
                    break;
            }
            break;
        case WM_CLOSE: {
            DestroyWindow(hDlg);
            return TRUE;
        }

        case WM_DESTROY: {
            hObjDialog = nullptr;
            return TRUE;
        }
        break;
        default: ;
    }
    return (INT_PTR) FALSE;
}

void Obj::buildObjFromNavp(bool alsoLoadIntoUi) {
    NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    std::string fileName = navKitSettings.outputFolder + "\\outputNavp.obj";
    Gui &gui = Gui::getInstance();
    gui.showLog = true;

    Logger::log(NK_INFO, ("Building obj from loaded navp. Saving to: " + fileName).c_str());
    Navp &navp = Navp::getInstance();
    std::vector<Vec3> vertices;
    std::vector<std::vector<int> > faces;
    std::map<Vec3, int> pointsToIndexMap;
    int pointCount = 1;
    for (const auto &[m_area, m_edges]: navp.navMesh->m_areas) {
        std::vector<int> face;
        for (auto edge: m_edges) {
            auto point = edge->m_pos;
            if (!pointsToIndexMap.contains(point)) {
                vertices.push_back(point);
                pointsToIndexMap[point] = pointCount++;
            }
            face.push_back(pointsToIndexMap[point]);
        }
        faces.push_back(face);
    }
    std::ofstream f(fileName);
    f.clear();
    for (auto v: vertices) {
        f << "v " << v.X << " " << v.Z << " " << -v.Y << "\n";
    }
    for (auto face: faces) {
        f << "f ";
        for (auto vi: face) {
            f << vi;
            if (vi != face.back()) {
                f << " ";
            }
        }
        f << "\n";
    }
    f.close();
    Logger::log(NK_INFO, "Done Building obj from loaded navp.");
    if (alsoLoadIntoUi) {
        generatedObjName = "outputNavp.obj";
        objLoaded = false;
        blenderObjGenerationDone = true;
        loadObj = true;
        Menu::updateMenuState();
    }
}

void Obj::buildObjFromScene() {
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    const Scene &scene = Scene::getInstance();
    objLoaded = false;
    Menu::updateMenuState();
    startedObjGeneration = true;
    std::string buildOutputFileType = blendFileAndObjBuild
                                          ? "both"
                                          : blendFileOnlyBuild
                                                ? "blend"
                                                : "obj";
    Logger::log(NK_INFO, "Generating %s from nav.json file.", buildOutputFileType.c_str());
    std::string command = "\"";
    command += navKitSettings.blenderPath;
    command += "\" -b --factory-startup -P glacier2obj.py -- \""; //--debug-all
    command += scene.lastLoadSceneFile;
    command += "\" \"";
    command += navKitSettings.outputFolder;
    command += "\\output." + buildOutputFileType + "\"";
    if (meshTypeForBuild == ALOC) {
        command += " ALOC ";
    } else {
        command += " PRIM ";
    }
    command += buildPrimLodsString();
    if (sceneMeshBuildType == COPY) {
        command += " copy ";
    } else {
        command += " instance ";
    }

    if (filterToIncludeBox) {
        command += " true";
    } else
    {
        command += " false";
    }

    if (glacier2ObjDebugLogsEnabled) {
        command += " true";
    }
    blenderObjStarted = true;
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    generatedObjName = "output.obj";

    backgroundWorker.emplace(
        &CommandRunner::runCommand, CommandRunner::getInstance(), command, "Glacier2ObjBlender.log", [this,
            buildOutputFileType] {
            Logger::log(NK_INFO, "Finished generating %s from nav.json file.", buildOutputFileType.c_str());
            blenderObjGenerationDone = true;
            if (blendFileAndObjBuild || blendFileOnlyBuild) {
                blendFileBuilt = true;
            }
        }, [this] {
            errorBuilding = true;
        });
}

void Obj::extractAlocsOrPrimsAndStartObjBuild() {
    if (skipExtractingAlocsOrPrims) {
        Logger::log(NK_INFO, "Skipping extraction of Alocs / Prims from Rpkg files.");
        doneExtractingAlocsOrPrims = true;
        Menu::updateMenuState();
        return;
    }
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    const std::string alocOrPrimFolder = navKitSettings.outputFolder + (meshTypeForBuild == ALOC ? "\\aloc" : "\\prim");

    struct stat folderExists{};
    if (const int statRC = stat(alocOrPrimFolder.data(), &folderExists); statRC != 0) {
        if (errno == ENOENT) {
            if (const int status = _mkdir(alocOrPrimFolder.c_str()); status != 0) {
                Logger::log(NK_ERROR, "Error creating Aloc / Prim folder");
                errorExtracting = true;
                return;
            }
        }
    }
    const Scene &scene = Scene::getInstance();
    const std::string &fileNameString = scene.lastLoadSceneFile;
    const std::string runtimeFolder = navKitSettings.hitmanFolder + "\\Runtime";
    const std::string retailFolder = navKitSettings.hitmanFolder + "\\Retail";
    const std::string gameVersion = "HM3";
    const std::string navJsonFilePath = fileNameString;

    extractingAlocsOrPrims = true;
    Menu::updateMenuState();
    const int result = run_extraction_wrapper(retailFolder.c_str(),
                           gameVersion.c_str(),
                           navJsonFilePath.c_str(),
                           runtimeFolder.c_str(),
                           alocOrPrimFolder.c_str(),
                           (meshTypeForBuild == ALOC) ? "ALOC" : "PRIM",
                           Logger::rustLogCallback);
    if (result) {
        Logger::log(NK_ERROR, "Error extracting Alocs / Prims from Rpkg files.");
        errorExtracting = true;
        return;
    }
    Logger::log(NK_INFO, "Finished extracting Alocs / Prims from Rpkg files.");
    doneExtractingAlocsOrPrims = true;
    Menu::updateMenuState();
}

char *Obj::openSetBlenderFileDialog(const char *lastBlenderFile) {
    nfdu8filteritem_t filters[1] = {{"Exe files", "exe"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

void Obj::finalizeExtractAlocsOrPrims() {
    if (errorExtracting) {
        errorBuilding = false;
        startedObjGeneration = false;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
        errorExtracting = false;
        extractingAlocsOrPrims = false;
        SceneExtract &sceneExtract = SceneExtract::getInstance();
        sceneExtract.alsoBuildAll = false;
        sceneExtract.alsoBuildObj = false;
    }
    if (doneExtractingAlocsOrPrims) {
        doneExtractingAlocsOrPrims = false;
        const Scene &scene = Scene::getInstance();
        const std::string &fileNameString = scene.lastLoadSceneFile;
        extractingAlocsOrPrims = false;
        buildObjFromScene();
        Logger::log(NK_INFO, ("Done loading nav.json file: '" + fileNameString + "'.").c_str());
        errorExtracting = false;
    }
    Menu::updateMenuState();
}

void Obj::finalizeObjBuild() {
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    if (blenderObjGenerationDone) {
        startedObjGeneration = false;
        objToLoad = navKitSettings.outputFolder;
        objToLoad += "\\" + generatedObjName;
        loadObj = !blendFileOnlyBuild;
        lastObjFileName = navKitSettings.outputFolder;
        lastObjFileName += generatedObjName;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
        sceneExtract.alsoBuildObj = false;
        blendFileOnlyBuild = false;
    }
    if (errorBuilding) {
        errorBuilding = false;
        startedObjGeneration = false;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
        objLoaded = false;
        sceneExtract.alsoBuildAll = false;
        sceneExtract.alsoBuildObj = false;
        blendFileOnlyBuild = false;
    }
    Menu::updateMenuState();
}

void Obj::copyFile(const std::string &from, const std::string &to, const std::string& filetype) {
    if (from == to) {
        Logger::log(NK_ERROR, "Cannot overwrite current %s file: %s", filetype.c_str(), from.c_str());
        return;
    }

    const auto start = std::chrono::high_resolution_clock::now();
    Logger::log(NK_INFO, "Copying %s from '%s' to '%s'...", filetype.c_str(), from.c_str(), to.c_str());

    try {
        std::filesystem::copy(from, to, std::filesystem::copy_options::overwrite_existing);

        const auto end = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        Logger::log(NK_INFO, "Finished saving %s in %lld ms.", filetype.c_str(), duration.count());
    } catch (const std::filesystem::filesystem_error &e) {
        Logger::log(NK_ERROR, "Error copying %s file: %s", filetype.c_str(), e.what());
    }
}

void Obj::saveObjMesh(char *objToCopy, char *newFileName) {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Obj to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    backgroundWorker.emplace(&Obj::copyFile, objToCopy, newFileName, "Obj");
}

void Obj::saveBlendMesh(std::string objToCopy, std::string newFileName) {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Blend to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    backgroundWorker.emplace(&Obj::copyFile, objToCopy, newFileName, "Blend");
}

void Obj::loadObjMesh() {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Obj from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    const auto start = std::chrono::high_resolution_clock::now();
    if (const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        recastAdapter.loadInputGeom(objToLoad) && recastAdapter.getVertCount() != 0) {
        if (objLoadDone.empty()) {
            objLoadDone.push_back(true);
            // Disabling for now. Maybe would be good to add a button to resize the scene bbox to the obj.
            // But it's pretty rare for there to not be an include box in the scene, so don't want to override
            // a bbox that has been manually set.
            // if (scene.includeBox.id == ZPathfinding::PfBoxes::NO_INCLUDE_BOX_FOUND) {
                // Scene &scene = Scene::getInstance();
                // const float pos[3] = {
                //     scene.bBoxPos[0],
                //     scene.bBoxPos[1],
                //     scene.bBoxPos[2]
                // };
                // const float size[3] = {
                //     scene.bBoxScale[0],
                //     scene.bBoxScale[1],
                //     scene.bBoxScale[2]
                // };
            //     scene.setBBox(pos, size);
            // }
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
            msg = "Finished loading Obj in ";
            msg += std::to_string(duration.count());
            msg += " seconds";
            Logger::log(NK_INFO, msg.data());
        }
    } else {
        Logger::log(NK_ERROR, "Error loading obj.");
        if (recastAdapter.getVertCount() == 0) {
            Logger::log(NK_ERROR, "Cannot load Obj, Obj has 0 vertices.");
        }
        SceneExtract::getInstance().alsoBuildAll = false;
        Menu::updateMenuState();
    }
    objToLoad.clear();
}

void Obj::renderObj() {
    RecastAdapter::getInstance().drawInputGeom();
}

char *Obj::openLoadObjFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

char *Obj::openSaveObjFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output");
}

char *Obj::openSaveBlendFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Blend files", "blend"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output");
}

void Obj::setLastLoadFileName(const char *fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadObjName = fileName;
        lastObjFileName = loadObjName;
        loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
    }
}

void Obj::setLastSaveFileName(const char *fileName) {
    lastSaveObjFileName = fileName;
    loadObjName = loadObjName.substr(loadObjName.find_last_of("/\\") + 1);
}

void Obj::handleOpenObjClicked() {
    if (const char *fileName = openLoadObjFileDialog()) {
        setLastLoadFileName(fileName);
        objLoaded = false;
        objToLoad = fileName;
        loadObj = true;
        Menu::updateMenuState();
    }
}

void Obj::handleSaveObjClicked() {
    if (const char *fileName = openSaveObjFileDialog()) {
        loadObjName = fileName;
        setLastSaveFileName(fileName);
        saveObjMesh(lastObjFileName.data(), lastSaveObjFileName.data());
        saveObjName = loadObjName;
    }
}

void Obj::handleSaveBlendClicked() {
    if (const char *fileName = openSaveBlendFileDialog()) {
        const std::string fileNameStr = fileName;
        const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
        const std::string blendOutputFileName = navKitSettings.outputFolder + "\\output.blend";
        saveBlendMesh(blendOutputFileName, fileNameStr);
    }
}

bool Obj::canLoad() const {
    return objToLoad.empty();
}

bool Obj::canBuildObjFromNavp() {
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    const Navp &navp = Navp::getInstance();
    return navKitSettings.outputSet && navKitSettings.blenderSet && navp.navpLoaded;
}

bool Obj::canBuildObjFromScene() const {
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    const Scene &scene = Scene::getInstance();
    return navKitSettings.hitmanSet && navKitSettings.outputSet && !extractingAlocsOrPrims && navKitSettings.blenderSet
           &&
           scene.sceneLoaded && !blenderObjStarted && !blenderObjGenerationDone;
}

bool Obj::canSaveBlend() const {
    return blendFileBuilt;
}

bool Obj::canBuildBlendFromScene() const {
    // Currently this is the same as Obj::canBuildObjFromScene.
    // Its expected this may change if the blend export includes features OBJ can't handle - like lights.
    return canBuildObjFromScene();
}

bool Obj::canBuildBlendAndObjFromScene() const {
    return canBuildObjFromScene();
}

void Obj::handleBuildObjFromSceneClicked() {
    backgroundWorker.emplace(&Obj::extractAlocsOrPrimsAndStartObjBuild, this);
}

void Obj::handleBuildBlendFromSceneClicked() {
    blendFileOnlyBuild = true;
    backgroundWorker.emplace(&Obj::extractAlocsOrPrimsAndStartObjBuild, this);
}

void Obj::handleBuildBlendAndObjFromSceneClicked() {
    blendFileAndObjBuild = true;
    backgroundWorker.emplace(&Obj::extractAlocsOrPrimsAndStartObjBuild, this);
}

void Obj::handleBuildObjFromNavpClicked() {
    return buildObjFromNavp(true);
}

void Obj::finalizeLoad() {
    if (loadObj) {
        lastObjFileName = objToLoad;
        objectTriangleRanges.clear();
        RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        recastAdapter.selectedObject = "";
        recastAdapter.markerPositionSet = false;
        std::string msg = "Loading Obj file: '";
        msg += objToLoad;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        backgroundWorker.emplace(&Obj::loadObjMesh, this);
        loadObj = false;
    }

    if (!objLoadDone.empty()) {
        if (RecastAdapter &recastAdapter = RecastAdapter::getInstance(); recastAdapter.inputGeom) {
            recastAdapter.handleMeshChanged();
            Navp::updateExclusionBoxConvexVolumes();
        }
        objLoaded = true;
        objLoadDone.clear();
        Menu::updateMenuState();
        if (Navp &navp = Navp::getInstance(); SceneExtract::getInstance().alsoBuildAll && navp.canBuildNavp()) {
            Logger::log(NK_INFO, "Obj load complete, building Navp...");
            navp.handleBuildNavpClicked();
        }
    }
}

std::string Obj::buildPrimLodsString() const {
    std::string primLodsStr;
    for (const bool primLod: primLods) {
        primLodsStr += primLod ? '1' : '0';
    }
    return primLodsStr;
}

void Obj::loadSettings() {
    const PersistedSettings &persistedSettings = PersistedSettings::getInstance();
    meshTypeForBuild = strcmp(persistedSettings.getValue("Obj", "meshTypeForBuild", "ALOC"), "ALOC") == 0 ? ALOC : PRIM;
    const std::string primLodsStr = persistedSettings.getValue("Obj", "primLods", "11111111");
    for (int i = 0; i < 8; ++i) {
        if (i < primLodsStr.size()) {
            primLods[i] = primLodsStr[i] == '1';
        } else {
            primLods[i] = true;
        }
    }
    sceneMeshBuildType = strcmp(persistedSettings.getValue("Obj", "sceneMeshBuildType", "COPY"), "COPY") == 0 ? COPY : INSTANCE;
    skipExtractingAlocsOrPrims = strcmp(persistedSettings.getValue("Obj", "skipExtractingAlocsOrPrims", "false"), "true") == 0;
    filterToIncludeBox = strcmp(persistedSettings.getValue("Obj", "filterToIncludeBox", "true"), "true") == 0;
    glacier2ObjDebugLogsEnabled = strcmp(persistedSettings.getValue("Obj", "glacier2ObjDebugLogsEnabled", "false"), "true") == 0;
}

void Obj::saveObjSettings() const {
    PersistedSettings &persistedSettings = PersistedSettings::getInstance();

    const char *meshTypeStr = (meshTypeForBuild == PRIM) ? "PRIM" : "ALOC";
    persistedSettings.setValue("Obj", "meshTypeForBuild", meshTypeStr);

    const std::string primLodsStr = buildPrimLodsString();
    persistedSettings.setValue("Obj", "primLods", primLodsStr);

    const char *buildTypeStr = (sceneMeshBuildType == COPY) ? "COPY" : "INSTANCE";
    persistedSettings.setValue("Obj", "sceneMeshBuildType", buildTypeStr);

    const char *skipRpkgExtract = skipExtractingAlocsOrPrims ? "true" : "false";
    persistedSettings.setValue("Obj", "skipExtractingAlocsOrPrims", skipRpkgExtract);

    const char *filterEnabled = filterToIncludeBox ? "true" : "false";
    persistedSettings.setValue("Obj", "filterToIncludeBox", filterEnabled);

    const char *debugEnabled = glacier2ObjDebugLogsEnabled ? "true" : "false";
    persistedSettings.setValue("Obj", "glacier2ObjDebugLogsEnabled", debugEnabled);

    persistedSettings.save();
}

void Obj::resetDefaults() {
    meshTypeForBuild = ALOC;
    for (bool & primLod : primLods) {
        primLod = true;
    }
    sceneMeshBuildType = COPY;
    skipExtractingAlocsOrPrims = false;
    filterToIncludeBox = true;
    glacier2ObjDebugLogsEnabled = false;
}

void Obj::showObjDialog() {
    if (hObjDialog) {
        SetForegroundWindow(hObjDialog);
        return;
    }
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    HWND hParentWnd = Renderer::hwnd;
    hObjDialog = CreateDialogParam(
        hInstance,
        MAKEINTRESOURCE(IDD_OBJ_SETTINGS),
        hParentWnd,
        ObjSettingsDialogProc,
        reinterpret_cast<LPARAM>(this)
    );

    if (hObjDialog) {
        if (HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON))) {
            SendMessage(hObjDialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessage(hObjDialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }
        RECT parentRect, dialogRect;
        GetWindowRect(hParentWnd, &parentRect);
        GetWindowRect(hObjDialog, &dialogRect);

        const int parentWidth = parentRect.right - parentRect.left;
        const int parentHeight = parentRect.bottom - parentRect.top;
        const int dialogWidth = dialogRect.right - dialogRect.left;
        const int dialogHeight = dialogRect.bottom - dialogRect.top;

        const int newX = parentRect.left + (parentWidth - dialogWidth) / 2;
        const int newY = parentRect.top + (parentHeight - dialogHeight) / 2;

        SetWindowPos(hObjDialog, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ShowWindow(hObjDialog, SW_SHOW);
    }
}
