#include "../../include/NavKit/module/Obj.h"

#include <direct.h>
#include <SDL.h>
#include <GL/glew.h>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <thread>
#include <vector>
#include <future>
#include <assimp/scene.h>

#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/navkit-rpkg-lib/navkit-rpkg-lib.h"
#include "../../include/NavWeakness/NavPower.h"

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
             blendFileBuilt(false),
             extractTextures(false),
             applyTextures(false) {
}

HWND Obj::hObjDialog = nullptr;

GLuint Obj::tileTextureId = 0;

void Obj::loadTileTexture() {
    if (tileTextureId != 0) return;

    if (SDL_Surface* loadedSurface = SDL_LoadBMP("tile.bmp")) {
        SDL_Surface* formattedSurface = SDL_ConvertSurfaceFormat(loadedSurface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loadedSurface);

        if (!formattedSurface) {
            Logger::log(NK_ERROR, "Failed to convert BMP surface: %s", SDL_GetError());
        } else {
            glGenTextures(1, &tileTextureId);
            glBindTexture(GL_TEXTURE_2D, tileTextureId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, formattedSurface->w, formattedSurface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, formattedSurface->pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            SDL_FreeSurface(formattedSurface);
        }
    } else {
        Logger::log(NK_ERROR, "Failed to load tile.bmp: %s", SDL_GetError());
    }
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
        &CommandRunner::runCommand, CommandRunner::getInstance(), command, "Glacier2Obj.log", [this,
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

void Obj::extractResourcesAndStartObjBuild() {
    const std::string meshFileType = meshTypeForBuild == ALOC ? "ALOC" : "PRIM";
    const std::string meshFileTypeLower = meshTypeForBuild == ALOC ? "aloc" : "prim";
    if (skipExtractingAlocsOrPrims) {
        Logger::log(NK_INFO, "Skipping extraction of %ss from Rpkg files.", meshFileType.c_str());
        doneExtractingAlocsOrPrims = true;
        Menu::updateMenuState();
        return;
    }
    const NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    const std::string alocOrPrimFolder = navKitSettings.outputFolder + "\\" + meshFileTypeLower;

    struct stat folderExists{};
    if (const int statRC = stat(alocOrPrimFolder.data(), &folderExists); statRC != 0) {
        if (errno == ENOENT) {
            if (const int status = _mkdir(alocOrPrimFolder.c_str()); status != 0) {
                Logger::log(NK_ERROR, "Error creating %s folder", meshFileType.c_str());
                errorExtracting = true;
                return;
            }
        }
    }
    const Scene &scene = Scene::getInstance();
    const std::string &fileNameString = scene.lastLoadSceneFile;
    const std::string runtimeFolder = navKitSettings.hitmanFolder + "\\Runtime";
    const std::string retailFolder = navKitSettings.hitmanFolder + "\\Retail";
    const std::string navJsonFilePath = fileNameString;

    extractingAlocsOrPrims = true;
    Menu::updateMenuState();
    const int result = extract_scene_mesh_resources(
                           navJsonFilePath.c_str(),
                           runtimeFolder.c_str(),
                           Rpkg::partitionManager,
                           alocOrPrimFolder.c_str(),
                           meshFileType.c_str(),
                           Logger::rustLogCallback);
    if (result) {
        Logger::log(NK_ERROR, "Error extracting %ss from Rpkg files.", meshFileType.c_str());
        errorExtracting = true;
        return;
    }

    if (shouldExtractTextures()) {
        simdjson::ondemand::parser parser;
        Logger::log(NK_INFO, "Extracting MATIs from Rpkg files.");
        primHashToMatiHash.clear();
        for (auto mesh : scene.meshes) {
            const auto referencesRustList = get_all_referenced_hashes_by_hash_from_rpkg_files(
                mesh.primHash.c_str(),
                Rpkg::partitionManager,
                Logger::rustLogCallback);
            if (referencesRustList == nullptr) {
                Logger::log(NK_ERROR, "Error getting references from %ss from Rpkg files.", mesh.primHash.c_str());
                continue;
                // errorExtracting = true;
                // return;
            }
            for (int i = 0; i < referencesRustList->length; i++) {
                std::string matiHash = get_string_from_list(referencesRustList, i);
                if (!primHashToMatiHash.contains(matiHash)) {
                    primHashToMatiHash.insert({mesh.primHash, {}});
                }
                primHashToMatiHash[mesh.primHash].push_back(matiHash);

                if (char* matiJson = get_mati_json_by_hash(matiHash.c_str(), Rpkg::hashList, Rpkg::partitionManager,
                                                           Logger::rustLogCallback); matiJson != nullptr) {
                    std::string matiJsonString = matiJson;
                    const auto json = simdjson::padded_string(matiJsonString);
                    auto jsonDocument = parser.iterate(json);
                    Json::Mati mati;
                    matiHashToMati.insert({matiHash, mati});
                    try {
                        mati.readJson(jsonDocument);
                    } catch (const std::exception& e) {
                        Logger::log(NK_ERROR, e.what());
                        free_string(matiJson);
                        return;
                    }
                    free_string(matiJson);
                    Logger::log(NK_INFO, "Found diffuse texture %s for mati %s with mati id %s for mesh %s.",
                                mati.properties.diffuseIoiString.value.c_str(), matiHash.c_str(), mati.id.c_str(),
                                mesh.primHash.c_str());
                }
            }
        }
    }
    Logger::log(NK_INFO, "Finished extracting {}s from Rpkg files.", meshFileType.c_str());
    doneExtractingAlocsOrPrims = true;
    Menu::updateMenuState();
}

char *Obj::openSetBlenderFileDialog(const char *lastBlenderFile) {
    nfdu8filteritem_t filters[1] = {{"Exe files", "exe"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

void Obj::finalizeExtractResources() {
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

bool Obj::shouldExtractTextures() const {
    return true;
    // return applyTextures && extractTextures;
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

    auto recastFuture = std::async(std::launch::async, [this]() {
        Logger::log(NK_INFO, "Loading Obj model data to Recast...");
        const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        int result = recastAdapter.loadInputGeom(objToLoad) && recastAdapter.getVertCount() != 0;
        Logger::log(NK_INFO, "Done loading Obj model data to Recast.");
        return result;
    });

    auto modelFuture = std::async(std::launch::async, [this]() {
        Logger::log(NK_INFO, "Loading Obj model data to rendering system...");
        model.loadModelData(objToLoad);
        Logger::log(NK_INFO, "Done loading Obj model data to rendering system.");
    });

    if (recastFuture.get()) {
        modelFuture.wait();
        if (objLoadDone.empty()) {
            objLoadDone.push_back(true);
            // Disabling for now. Maybe would be good to add a button to resize the scene bbox to the obj.
            // But it's pretty rare for there to not be an include box in the scene, so don't want to override
            // a bbox that has been manually set.
            // if (scene.includeBox.id == Json::PfBoxes::NO_INCLUDE_BOX_FOUND) {
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
        modelFuture.wait();
        Logger::log(NK_ERROR, "Error loading obj.");
        const RecastAdapter &recastAdapter = RecastAdapter::getInstance();
        if (recastAdapter.getVertCount() == 0) {
            Logger::log(NK_ERROR, "Cannot load Obj, Obj has 0 vertices.");
        }
        model.meshes.clear();
        SceneExtract::getInstance().alsoBuildAll = false;
        Menu::updateMenuState();
    }
}

void Obj::renderObj() const {
    Renderer &renderer = Renderer::getInstance();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    renderer.shader.use();

    if (tileTextureId != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tileTextureId);
        renderer.shader.setInt("tileTexture", 0);
    }

    auto modelTransform = glm::mat4(1.0f);
    modelTransform = glm::translate(modelTransform, glm::vec3(0.0f, 0.0f, 0.0f));
    modelTransform = glm::scale(modelTransform, glm::vec3(1.0f, 1.0f, 1.0f));
    renderer.shader.setMat4("projection", renderer.projection);
    renderer.shader.setMat4("view", renderer.view);
    renderer.shader.setMat4("model", modelTransform);
    renderer.shader.setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(modelTransform))));
    renderer.shader.setVec4("objectColor", glm::vec4(0.50f, 0.5f, 0.5f, 1.0f));

    model.draw(renderer.shader, renderer.projection * renderer.view);
}

void Obj::renderObjUsingRecast() {
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
        && scene.sceneLoaded && !blenderObjStarted && !blenderObjGenerationDone && Rpkg::extractionDataInitComplete;
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
    backgroundWorker.emplace(&Obj::extractResourcesAndStartObjBuild, this);
}

void Obj::handleBuildBlendFromSceneClicked() {
    blendFileOnlyBuild = true;
    backgroundWorker.emplace(&Obj::extractResourcesAndStartObjBuild, this);
}

void Obj::handleBuildBlendAndObjFromSceneClicked() {
    blendFileAndObjBuild = true;
    backgroundWorker.emplace(&Obj::extractResourcesAndStartObjBuild, this);
}

void Obj::handleBuildObjFromNavpClicked() {
    return buildObjFromNavp(true);
}

void Obj::finalizeLoad() {
    if (loadObj) {
        lastObjFileName = objToLoad;
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
            Logger::log(NK_INFO, "Creating OpenGL buffers for model...");
            loadTileTexture();
            model.initGL();
            Logger::log(NK_INFO, "Finished creating OpenGL buffers.");

            recastAdapter.handleMeshChanged();
            Navp::updateExclusionBoxConvexVolumes();
        }
        objLoaded = true;
        objLoadDone.clear();
        objToLoad.clear();
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
