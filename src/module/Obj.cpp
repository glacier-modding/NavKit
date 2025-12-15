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
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"
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
             glacier2ObjDebugLogsEnabled(false),
             errorBuilding(false),
             errorExtracting(false),
             extractingAlocsOrPrims(false),
             blendFileOnlyExtract(false),
             doneExtractingAlocsOrPrims(false),
             doObjHitTest(false),
             meshTypeForBuild(ALOC),
             primLods{true, true, true, true, true, true, true, true} {
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
}

INT_PTR CALLBACK Obj::ObjSettingsDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    Obj &obj = getInstance();
    switch (message) {
        case WM_INITDIALOG: {
            updateObjDialogControls(hDlg);
            return (INT_PTR) TRUE;
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
                    return (INT_PTR) TRUE;
                }

                case WM_CLOSE:
                    DestroyWindow(hDlg);
                    return (INT_PTR) TRUE;

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
    Settings &settings = Settings::getInstance();
    std::string fileName = settings.outputFolder + "\\outputNavp.obj";
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

void Obj::buildObj() {
    const Settings &settings = Settings::getInstance();
    const Scene &scene = Scene::getInstance();
    objLoaded = false;
    Menu::updateMenuState();
    startedObjGeneration = true;
    std::string buildOutputFileType = blendFileOnlyExtract ? "blend" : "obj";
    Logger::log(NK_INFO, "Generating %s from nav.json file.", buildOutputFileType.c_str());
    std::string command = "\"";
    command += settings.blenderPath;
    command += "\" -b --factory-startup -P glacier2obj.py -- \""; //--debug-all
    command += scene.lastLoadSceneFile;
    command += "\" \"";
    command += settings.outputFolder;
    command += "\\output." + buildOutputFileType + "\"";
    if (meshTypeForBuild == ALOC) {
        command += " ALOC ";
    } else {
        command += " PRIM ";
    }
    command += buildPrimLodsString();
    if (glacier2ObjDebugLogsEnabled) {
        command += " True";
    }
    blenderObjStarted = true;
    Gui &gui = Gui::getInstance();
    gui.showLog = true;
    generatedObjName = "output.obj";

    backgroundWorker.emplace(
        &CommandRunner::runCommand, CommandRunner::getInstance(), command, "Glacier2ObjBlender.log", [this, buildOutputFileType] {
            Logger::log(NK_INFO, "Finished generating %s from nav.json file.", buildOutputFileType.c_str());
            blenderObjGenerationDone = true;
        }, [this] {
            errorBuilding = true;
        });
}

void Obj::extractAlocsOrPrims() {
    Settings &settings = Settings::getInstance();
    std::string retailFolder = "\"";
    retailFolder += settings.hitmanFolder;
    retailFolder += "\\Retail\"";
    std::string gameVersion = "HM3";
    std::string navJsonFilePath = "\"";
    navJsonFilePath += settings.outputFolder;
    navJsonFilePath += "\\output.nav.json\"";
    std::string runtimeFolder = "\"";
    runtimeFolder += settings.hitmanFolder;
    runtimeFolder += "\\Runtime\"";
    std::string alocOrPrimFolder;
    alocOrPrimFolder += settings.outputFolder;
    alocOrPrimFolder += (meshTypeForBuild == ALOC) ? "\\aloc" : "\\prim";

    struct stat folderExists{};
    if (int statRC = stat(alocOrPrimFolder.data(), &folderExists); statRC != 0) {
        if (errno == ENOENT) {
            if (int status = _mkdir(alocOrPrimFolder.c_str()); status != 0) {
                Logger::log(NK_ERROR, "Error creating Aloc / Prim folder");
            }
        }
    }

    std::string command = "Glacier2Obj.exe ";
    command += retailFolder;
    command += " ";
    command += gameVersion;
    command += " ";
    command += navJsonFilePath;
    command += " ";
    command += runtimeFolder;
    command += " \"";
    command += alocOrPrimFolder;
    command += "\" ";
    command += (meshTypeForBuild == ALOC) ? "ALOC" : "PRIM";

    extractingAlocsOrPrims = true;
    Menu::updateMenuState();
    std::jthread commandThread(
        &CommandRunner::runCommand, CommandRunner::getInstance(), command,
        "Glacier2ObjExtract.log", [this] {
            Logger::log(NK_INFO, "Finished extracting Alocs / Prims from Rpkg files file.");
            doneExtractingAlocsOrPrims = true;
            Menu::updateMenuState();
        }, [this] {
            errorExtracting = true;
        });
}

char *Obj::openSetBlenderFileDialog(const char *lastBlenderFile) {
    nfdu8filteritem_t filters[1] = {{"Exe files", "exe"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

void Obj::finalizeExtractAlocsOrPrims() {
    Settings &settings = Settings::getInstance();

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
        std::string sceneFile = settings.outputFolder;
        sceneFile += "\\output.nav.json";
        Scene &scene = Scene::getInstance();
        const std::string &fileNameString = sceneFile;
        extractingAlocsOrPrims = false;
        scene.lastLoadSceneFile = sceneFile;
        buildObj();
        Logger::log(NK_INFO, ("Done loading nav.json file: '" + fileNameString + "'.").c_str());
        errorExtracting = false;
    }
    Menu::updateMenuState();
}

void Obj::finalizeObjBuild() {
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    Settings &settings = Settings::getInstance();
    if (blenderObjGenerationDone) {
        startedObjGeneration = false;
        objToLoad = settings.outputFolder;
        objToLoad += "\\" + generatedObjName;
        loadObj = !blendFileOnlyExtract;
        lastObjFileName = settings.outputFolder;
        lastObjFileName += generatedObjName;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
        sceneExtract.alsoBuildObj = false;
        blendFileOnlyExtract = false;
    }
    if (errorBuilding) {
        errorBuilding = false;
        startedObjGeneration = false;
        blenderObjStarted = false;
        blenderObjGenerationDone = false;
        objLoaded = false;
        sceneExtract.alsoBuildAll = false;
        sceneExtract.alsoBuildObj = false;
        blendFileOnlyExtract = false;
    }
    Menu::updateMenuState();
}

void Obj::copyObjFile(const std::string &from, const std::string &to) {
    if (from == to) {
        Logger::log(NK_ERROR, "Cannot overwrite current obj file: %s", from.c_str());
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();
    Logger::log(NK_INFO, "Copying OBJ from '%s' to '%s'...", from.c_str(), to.c_str());

    try {
        std::filesystem::copy(from, to, std::filesystem::copy_options::overwrite_existing);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        Logger::log(NK_INFO, "Finished saving Obj in %lld ms.", duration.count());
    } catch (const std::filesystem::filesystem_error &e) {
        Logger::log(NK_ERROR, "Error copying file: %s", e.what());
    }
}

void Obj::saveObjMesh(char *objToCopy, char *newFileName) {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Obj to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    backgroundWorker.emplace(&Obj::copyObjFile, objToCopy, newFileName);
}

void Obj::loadObjMesh() {
    std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading Obj from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    auto start = std::chrono::high_resolution_clock::now();
    if (RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        recastAdapter.loadInputGeom(objToLoad) && recastAdapter.getVertCount() != 0) {
        if (objLoadDone.empty()) {
            objLoadDone.push_back(true);
            Scene &scene = Scene::getInstance();
            float pos[3] = {
                scene.bBoxPos[0],
                scene.bBoxPos[1],
                scene.bBoxPos[2]
            };
            float size[3] = {
                scene.bBoxScale[0],
                scene.bBoxScale[1],
                scene.bBoxScale[2]
            };
            scene.setBBox(pos, size);
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

char *Obj::openLoadObjFileDialog(const char *lastObjFolder) {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}


char *Obj::openSaveObjFileDialog(char *lastObjFolder) {
    nfdu8filteritem_t filters[1] = {{"Obj files", "obj"}};
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
    char *fileName = openLoadObjFileDialog(loadObjName.data());
    if (fileName) {
        setLastLoadFileName(fileName);
        objLoaded = false;
        objToLoad = fileName;
        loadObj = true;
        Menu::updateMenuState();
    }
}

void Obj::handleSaveObjClicked() {
    char *fileName = openSaveObjFileDialog(loadObjName.data());
    if (fileName) {
        loadObjName = fileName;
        setLastSaveFileName(fileName);
        saveObjMesh(lastObjFileName.data(), lastSaveObjFileName.data());
        saveObjName = loadObjName;
    }
}

bool Obj::canLoad() const {
    return objToLoad.empty();
}

bool Obj::canBuildObjFromNavp() {
    const Navp &navp = Navp::getInstance();
    const Settings &settings = Settings::getInstance();
    return settings.outputSet && settings.blenderSet && navp.navpLoaded;
}

bool Obj::canBuildObjFromScene() const {
    const Settings &settings = Settings::getInstance();
    const Scene &scene = Scene::getInstance();
    return settings.hitmanSet && settings.outputSet && !extractingAlocsOrPrims && settings.blenderSet &&
           scene.sceneLoaded && !blenderObjStarted && !blenderObjGenerationDone;
}

bool Obj::canBuildBlendFromScene() const {
    // Currently this is the same as Obj::canBuildObjFromScene.
    // Its expected this may change if the blend export includes features OBJ can't handle - like lights.
    return Obj::canBuildObjFromScene();
}

void Obj::handleBuildObjFromSceneClicked() {
    backgroundWorker.emplace(&Obj::extractAlocsOrPrims, this);
}

void Obj::handleBuildBlendFromSceneClicked() {
    blendFileOnlyExtract = true;
    backgroundWorker.emplace(&Obj::extractAlocsOrPrims, this);
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

void Obj::saveObjSettings() const {
    Settings &settings = Settings::getInstance();

    const char *meshTypeStr = (meshTypeForBuild == PRIM) ? "PRIM" : "ALOC";
    settings.setValue("Obj", "MeshTypeForBuild", meshTypeStr);

    const std::string primLodsStr = buildPrimLodsString();
    settings.setValue("Obj", "PrimLods", primLodsStr);

    settings.save();
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

        int parentWidth = parentRect.right - parentRect.left;
        int parentHeight = parentRect.bottom - parentRect.top;
        int dialogWidth = dialogRect.right - dialogRect.left;
        int dialogHeight = dialogRect.bottom - dialogRect.top;

        int newX = parentRect.left + (parentWidth - dialogWidth) / 2;
        int newY = parentRect.top + (parentHeight - dialogHeight) / 2;

        SetWindowPos(hObjDialog, NULL, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        ShowWindow(hObjDialog, SW_SHOW);
    }
}
