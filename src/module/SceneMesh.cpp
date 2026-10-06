#include "../../include/NavKit/module/SceneMesh.h"

#include <SDL.h>
#include <GL/glew.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <thread>
#include <vector>
#include <future>
#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <memory>

#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <array>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/util/CommandRunner.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/navkit-rpkg-lib/navkit-rpkg-lib.h"
#include "../../include/NavWeakness/NavPower.h"

SceneMesh::SceneMesh() :
    loadGlbName("Load GLB"), saveGlbName("Save GLB"), lastGlbFileName("Load GLB"), lastSaveGlbFileName("Save GLB"),
    glbLoaded(false), showGlb(true), loadGlb(false), startedSceneMeshGeneration(false),
    blenderSceneMeshBuildStarted(false), blenderSceneMeshGenerationDone(false), blendFileOnlyBuild(false),
    blendFileAndGlbBuild(false), filterToIncludeBox(true), onlyCollidable(true), errorBuilding(false),
    skipExtractingAlocsOrPrims(false), errorExtracting(false), extractingResources(false),
    doneExtractingAlocsOrPrims(false), doGlbHitTest(false), meshTypeForBuild(ALOC), sceneMeshBuildType(COPY),
    primLods{true, true, true, true, true, true, true, true}, blendFileBuilt(false), extractTextures(false),
    applyTextures(false) {}

wxDialog* SceneMesh::hSceneMeshDialog = nullptr;

GLuint SceneMesh::tileTextureId = 0;

void SceneMesh::loadTileTexture() {
    if (tileTextureId != 0) {
        return;
    }

    const std::string tilePath = FileUtil::getApplicationResourcePath("tile.bmp").string();
    if (SDL_Surface* loadedSurface = SDL_LoadBMP(tilePath.c_str())) {
        SDL_Surface* formattedSurface = SDL_ConvertSurfaceFormat(loadedSurface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loadedSurface);

        if (!formattedSurface) {
            Logger::log(NK_ERROR, "Failed to convert BMP surface: %s", SDL_GetError());
        } else {
            glGenTextures(1, &tileTextureId);
            glBindTexture(GL_TEXTURE_2D, tileTextureId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, formattedSurface->w, formattedSurface->h, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, formattedSurface->pixels);
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

void SceneMesh::updateSceneMeshDialogControls(wxDialog* dialog) {
    const SceneMesh& obj = getInstance();
    static_cast<wxRadioButton*>(dialog->FindWindow(IDC_RADIO_MESH_TYPE_ALOC))->SetValue(obj.meshTypeForBuild == ALOC);
    static_cast<wxRadioButton*>(dialog->FindWindow(IDC_RADIO_MESH_TYPE_PRIM))->SetValue(obj.meshTypeForBuild == PRIM);
    for (int i = 0; i < 8; ++i) {
        auto* control = static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_PRIM_LOD_1 + i));
        control->SetValue(obj.primLods[i]);
        control->Enable(obj.meshTypeForBuild == PRIM);
    }
    dialog->FindWindow(IDC_BUTTON_SELECT_ALL_LODS)->Enable(obj.meshTypeForBuild == PRIM);
    dialog->FindWindow(IDC_BUTTON_DESELECT_ALL_LODS)->Enable(obj.meshTypeForBuild == PRIM);
    static_cast<wxRadioButton*>(dialog->FindWindow(IDC_RADIO_BUILD_TYPE_COPY))
        ->SetValue(obj.sceneMeshBuildType == COPY);
    static_cast<wxRadioButton*>(dialog->FindWindow(IDC_RADIO_BUILD_TYPE_INSTANCE))
        ->SetValue(obj.sceneMeshBuildType == INSTANCE);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_SKIP_RPKG_EXTRACT))->SetValue(obj.skipExtractingAlocsOrPrims);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_FILTER_TO_INCLUDE_BOX))->SetValue(obj.filterToIncludeBox);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_ONLY_COLLIDABLE))->SetValue(obj.onlyCollidable);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_EXTRACT_TEXTURE_FILES))->SetValue(obj.extractTextures);
    static_cast<wxCheckBox*>(dialog->FindWindow(IDC_CHECK_APPLY_TEXTURES))->SetValue(obj.applyTextures);
}

void SceneMesh::showSceneMeshDialog() {
    if (hSceneMeshDialog) {
        hSceneMeshDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Scene Mesh Settings", wxDefaultPosition, wxSize(430, 450));
    hSceneMeshDialog = dialog;
    auto* content = new wxBoxSizer(wxVERTICAL);
    auto* meshType = new wxStaticBoxSizer(wxVERTICAL, dialog, "Mesh Type for Build");
    auto* aloc =
        new wxRadioButton(dialog, IDC_RADIO_MESH_TYPE_ALOC, "Aloc", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    auto* prim = new wxRadioButton(dialog, IDC_RADIO_MESH_TYPE_PRIM, "Prim");
    auto* meshTypeRow = new wxBoxSizer(wxHORIZONTAL);
    meshTypeRow->Add(aloc, 0, wxRIGHT, 12);
    meshTypeRow->Add(prim);
    meshType->Add(meshTypeRow, 0, wxALL, 6);
    content->Add(meshType, 0, wxEXPAND | wxALL, 6);

    auto* lodGroup = new wxStaticBoxSizer(wxVERTICAL, dialog, "Prim Level of Detail");
    auto* lodGrid = new wxFlexGridSizer(2, 4, 4, 10);
    for (int i = 0; i < 8; ++i) {
        const wxString label = i == 0 ? wxString("LOD 1 (Highest)") : wxString::Format("LOD %d", i + 1);
        lodGrid->Add(new wxCheckBox(dialog, IDC_CHECK_PRIM_LOD_1 + i, label));
    }
    lodGroup->Add(lodGrid, 0, wxALL, 6);
    auto* lodButtons = new wxBoxSizer(wxHORIZONTAL);
    lodButtons->Add(new wxButton(dialog, IDC_BUTTON_SELECT_ALL_LODS, "Select All"), 0, wxRIGHT, 6);
    lodButtons->Add(new wxButton(dialog, IDC_BUTTON_DESELECT_ALL_LODS, "Deselect All"));
    lodGroup->Add(lodButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
    content->Add(lodGroup, 0, wxEXPAND | wxALL, 6);

    auto* buildType = new wxStaticBoxSizer(wxHORIZONTAL, dialog, "Build Type for Blender File");
    buildType->Add(
        new wxRadioButton(dialog, IDC_RADIO_BUILD_TYPE_COPY, "Copy", wxDefaultPosition, wxDefaultSize, wxRB_GROUP), 0,
        wxALL, 6);
    buildType->Add(new wxRadioButton(dialog, IDC_RADIO_BUILD_TYPE_INSTANCE, "Instance"), 0, wxALL, 6);
    content->Add(buildType, 0, wxEXPAND | wxALL, 6);

    const std::array<std::pair<int, const char*>, 5> options = {{
        {IDC_CHECK_SKIP_RPKG_EXTRACT, "Skip Aloc / Prim Extraction from RPKG files"},
        {IDC_CHECK_FILTER_TO_INCLUDE_BOX, "Filter meshes to include box"},
        {IDC_CHECK_ONLY_COLLIDABLE, "Only include collidable meshes"},
        {IDC_CHECK_EXTRACT_TEXTURE_FILES, "Extract texture TEXT files from RPKG files"},
        {IDC_CHECK_APPLY_TEXTURES, "Apply textures to scene mesh"},
    }};
    for (const auto& [id, label] : options) {
        content->Add(new wxCheckBox(dialog, id, label), 0, wxALL, 6);
    }
    auto* reset = new wxButton(dialog, IDC_BUTTON_RESET_DEFAULTS, "Reset Defaults");
    content->Add(reset, 0, wxALIGN_RIGHT | wxALL, 8);
    dialog->SetSizer(content);
    updateSceneMeshDialogControls(dialog);

    auto& sceneMesh = getInstance();
    const auto saveCheckbox = [dialog, &sceneMesh](const int id, bool& setting, const char* name) {
        bool* settingPtr = &setting;
        dialog->Bind(
            wxEVT_CHECKBOX,
            [&sceneMesh, settingPtr, name](wxCommandEvent& event) {
                *settingPtr = event.IsChecked();
                sceneMesh.saveSceneMeshSettings();
                Logger::log(NK_INFO, "%s set to %s.", name, *settingPtr ? "true" : "false");
            },
            id);
    };
    dialog->Bind(wxEVT_RADIOBUTTON, [dialog, &sceneMesh](wxCommandEvent& event) {
        if (!event.IsChecked()) {
            return;
        }
        if (event.GetId() == IDC_RADIO_MESH_TYPE_ALOC || event.GetId() == IDC_RADIO_MESH_TYPE_PRIM) {
            sceneMesh.meshTypeForBuild = event.GetId() == IDC_RADIO_MESH_TYPE_ALOC ? ALOC : PRIM;
            Logger::log(
                NK_INFO, "Mesh type for build set to %s.", sceneMesh.meshTypeForBuild == ALOC ? "Aloc" : "Prim");
        } else {
            sceneMesh.sceneMeshBuildType = event.GetId() == IDC_RADIO_BUILD_TYPE_COPY ? COPY : INSTANCE;
            Logger::log(NK_INFO, "Scene Mesh Build type set to %s.",
                sceneMesh.sceneMeshBuildType == COPY ? "Copy" : "Instance");
        }
        sceneMesh.saveSceneMeshSettings();
        updateSceneMeshDialogControls(dialog);
    });
    saveCheckbox(IDC_CHECK_SKIP_RPKG_EXTRACT, sceneMesh.skipExtractingAlocsOrPrims, "Skip Extracting ALOCs or PRIMs");
    saveCheckbox(IDC_CHECK_FILTER_TO_INCLUDE_BOX, sceneMesh.filterToIncludeBox, "Filter to include box");
    saveCheckbox(IDC_CHECK_ONLY_COLLIDABLE, sceneMesh.onlyCollidable, "Only collidable meshes");
    saveCheckbox(IDC_CHECK_EXTRACT_TEXTURE_FILES, sceneMesh.extractTextures, "Extract textures");
    saveCheckbox(IDC_CHECK_APPLY_TEXTURES, sceneMesh.applyTextures, "Apply textures");
    for (int i = 0; i < 8; ++i) {
        dialog->Bind(
            wxEVT_CHECKBOX,
            [&sceneMesh, i](wxCommandEvent& event) {
                sceneMesh.primLods[i] = event.IsChecked();
                sceneMesh.saveSceneMeshSettings();
                Logger::log(NK_INFO, "Prim LODs set to %s.", sceneMesh.buildPrimLodsString().c_str());
            },
            IDC_CHECK_PRIM_LOD_1 + i);
    }
    dialog->Bind(
        wxEVT_BUTTON,
        [&sceneMesh, dialog](wxCommandEvent& event) {
            const bool check = event.GetId() == IDC_BUTTON_SELECT_ALL_LODS;
            for (bool& lod : sceneMesh.primLods) {
                lod = check;
            }
            sceneMesh.saveSceneMeshSettings();
            updateSceneMeshDialogControls(dialog);
            Logger::log(NK_INFO, "Prim LODs %s.", check ? "all selected" : "all deselected");
        },
        IDC_BUTTON_SELECT_ALL_LODS);
    dialog->Bind(
        wxEVT_BUTTON,
        [&sceneMesh, dialog](wxCommandEvent&) {
            for (bool& lod : sceneMesh.primLods) {
                lod = false;
            }
            sceneMesh.saveSceneMeshSettings();
            updateSceneMeshDialogControls(dialog);
            Logger::log(NK_INFO, "Prim LODs all deselected.");
        },
        IDC_BUTTON_DESELECT_ALL_LODS);
    reset->Bind(wxEVT_BUTTON, [&sceneMesh, dialog](wxCommandEvent&) {
        sceneMesh.resetDefaults();
        updateSceneMeshDialogControls(dialog);
        sceneMesh.saveSceneMeshSettings();
    });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hSceneMeshDialog == dialog) {
            hSceneMeshDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
}

void SceneMesh::buildGlbFromNavp(const bool alsoLoadIntoUi) {
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const std::string fileName = (std::filesystem::u8path(navKitSettings.outputFolder) / "outputNavp.glb").string();
    Gui& gui = Gui::getInstance();
    gui.showLog = true;

    Logger::log(NK_INFO, ("Building GLB from loaded navp. Saving to: " + fileName).c_str());
    const Navp& navp = Navp::getInstance();
    std::vector<Vec3> vertices;
    std::vector<std::vector<int>> faces;
    std::map<Vec3, int> pointsToIndexMap;
    int pointCount = 1;
    for (auto section : navp.navMesh->m_aSections) {
        for (auto navGraph : section.m_aNavGraphs) {
            for (const auto& [m_area, m_edges] : navGraph.m_areas) {
                std::vector<int> face;
                for (const auto edge : m_edges) {
                    auto point = edge->m_pos;
                    if (!pointsToIndexMap.contains(point)) {
                        vertices.push_back(point);
                        pointsToIndexMap[point] = pointCount++;
                    }
                    face.push_back(pointsToIndexMap[point]);
                }
                faces.push_back(face);
            }
        }
    }
    auto scene = std::make_unique<aiScene>();
    scene->mRootNode = new aiNode();
    scene->mRootNode->mName = "NavMesh";
    scene->mNumMaterials = 1;
    scene->mMaterials = new aiMaterial*[1]{new aiMaterial()};
    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh*[1]{new aiMesh()};
    aiMesh* mesh = scene->mMeshes[0];
    mesh->mName = "NavMesh";
    mesh->mMaterialIndex = 0;
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    mesh->mNumVertices = static_cast<unsigned int>(vertices.size());
    mesh->mVertices = new aiVector3D[mesh->mNumVertices];
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const Vec3& vertex = vertices[i];
        mesh->mVertices[i] = aiVector3D(vertex.X, vertex.Z, -vertex.Y);
    }
    std::vector<std::array<unsigned int, 3>> triangles;
    for (const auto& face : faces) {
        for (size_t i = 2; i < face.size(); ++i) {
            triangles.push_back({static_cast<unsigned int>(face[0] - 1), static_cast<unsigned int>(face[i - 1] - 1),
                static_cast<unsigned int>(face[i] - 1)});
        }
    }
    mesh->mNumFaces = static_cast<unsigned int>(triangles.size());
    mesh->mFaces = new aiFace[mesh->mNumFaces];
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        aiFace& face = mesh->mFaces[i];
        face.mNumIndices = 3;
        face.mIndices = new unsigned int[3]{triangles[i][0], triangles[i][1], triangles[i][2]};
    }
    scene->mRootNode->mNumMeshes = 1;
    scene->mRootNode->mMeshes = new unsigned int[1]{0};
    Assimp::Exporter exporter;
    if (exporter.Export(scene.get(), "glb2", fileName) != AI_SUCCESS) {
        Logger::log(NK_ERROR, "Failed to export Navp GLB: %s", exporter.GetErrorString());
        errorBuilding = true;
        return;
    }
    Logger::log(NK_INFO, "Done building GLB from loaded Navp.");
    if (alsoLoadIntoUi) {
        generatedGlbName = "outputNavp.glb";
        glbLoaded = false;
        blenderSceneMeshGenerationDone = true;
        loadGlb = true;
        Menu::updateMenuState();
    }
}

void SceneMesh::buildSceneMeshFromScene() {
    const auto start = std::chrono::high_resolution_clock::now();
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const Scene& scene = Scene::getInstance();
    glbLoaded = false;
    Menu::updateMenuState();
    startedSceneMeshGeneration = true;
    std::string buildOutputFileType = blendFileAndGlbBuild ? "both" : blendFileOnlyBuild ? "blend" : "glb";
    Logger::log(NK_INFO, "Generating %s from nav.json file.", buildOutputFileType.c_str());
    std::string command = "\"";
    command += navKitSettings.blenderPath;
    command += "\" -b --factory-startup -P \"";
    command += FileUtil::getApplicationResourcePath("Glacier2Glb.py").string();
    command += "\" -- \""; //--debug-all
    command += scene.lastLoadSceneFile;
    command += "\" \"";
    command += (std::filesystem::u8path(navKitSettings.outputFolder) / ("output." + buildOutputFileType)).string();
    command += "\"";
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
    } else {
        command += " false";
    }

    if (applyTextures && meshTypeForBuild == PRIM) {
        command += " true";
    } else {
        command += " false";
    }

    if (NavKitSettings::getInstance().showDebugLogs) {
        command += " true";
    }
    blenderSceneMeshBuildStarted = true;
    Gui& gui = Gui::getInstance();
    gui.showLog = true;
    generatedGlbName = "output.glb";

    backgroundWorker.emplace(
        &CommandRunner::runCommand, std::ref(CommandRunner::getInstance()), command, "Glacier2Glb.log",
        [this, buildOutputFileType, start] {
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            Logger::log(NK_INFO, "Finished generating %s from nav.json file in %lld ms.", buildOutputFileType.c_str(),
                duration.count());
            blenderSceneMeshGenerationDone = true;
            if (blendFileAndGlbBuild || blendFileOnlyBuild) {
                blendFileBuilt = true;
            }
        },
        [this] { errorBuilding = true; });
}

void SceneMesh::extractResourcesAndStartSceneMeshBuild() {
    extractingResources = true;
    Menu::updateMenuState();
    const std::string meshFileType = meshTypeForBuild == ALOC ? "ALOC" : "PRIM";
    const std::string meshFileTypeLower = meshTypeForBuild == ALOC ? "aloc" : "prim";
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const std::filesystem::path alocOrPrimPath =
        std::filesystem::u8path(navKitSettings.outputFolder) / meshFileTypeLower;
    const std::string alocOrPrimFolder = alocOrPrimPath.string();
    std::error_code directoryError;
    if (!std::filesystem::create_directories(alocOrPrimPath, directoryError) && directoryError) {
        Logger::log(NK_ERROR, "Error creating %s folder: %s", meshFileType.c_str(), directoryError.message().c_str());
        errorExtracting = true;
        return;
    }
    Scene& scene = Scene::getInstance();
    const std::string& fileNameString = scene.lastLoadSceneFile;
    const std::filesystem::path hitmanPath = std::filesystem::u8path(navKitSettings.hitmanFolder);
    const std::string runtimeFolder = (hitmanPath / "Runtime").string();
    const std::string retailFolder = (hitmanPath / "Retail").string();
    const std::string navJsonFilePath = fileNameString;
    if (skipExtractingAlocsOrPrims) {
        Logger::log(NK_INFO, "Skipping extraction of %ss from Rpkg files.", meshFileType.c_str());
    } else {
        Menu::updateMenuState();
        int result = extract_scene_mesh_resources(navJsonFilePath.c_str(), runtimeFolder.c_str(),
            Rpkg::partitionManager, alocOrPrimFolder.c_str(), meshFileType.c_str(), Logger::rustLogCallback);
        if (result) {
            Logger::log(NK_ERROR, "Error extracting %ss from Rpkg files.", meshFileType.c_str());
            errorExtracting = true;
            return;
        }
    }
    std::set<std::string> neededTextHashes{};
    if (shouldExtractTextures()) {
        simdjson::ondemand::parser parser;
        Logger::log(NK_INFO, "Extracting texture files from Rpkg files.");
        for (auto& mesh : scene.meshes) {
            if (!scene.primMatis.contains(mesh.primHash)) {
                Logger::log(NK_WARN, "Prim matis missing prim hash: {}.", mesh.primHash.c_str());
                continue;
            }
            for (auto primMatiHashes = scene.primMatis[mesh.primHash].matiHashes;
                const auto& matiHash : primMatiHashes) {
                if (!scene.matis.contains(matiHash)) {
                    Logger::log(
                        NK_WARN, "Matis missing for prim: {} mati: {}.", mesh.primHash.c_str(), matiHash.c_str());
                    continue;
                }
                auto diffuseHash = scene.matis[matiHash].diffuse;
                Logger::log(
                    NK_DEBUG, "Found diffuse texture %s for mesh %s.", diffuseHash.c_str(), mesh.primHash.c_str());
                neededTextHashes.insert(diffuseHash);
                auto normalHash = scene.matis[matiHash].normal;
                Logger::log(
                    NK_DEBUG, "Found normal texture %s for mesh %s.", normalHash.c_str(), mesh.primHash.c_str());
                neededTextHashes.insert(normalHash);
                auto specularHash = scene.matis[matiHash].specular;
                Logger::log(
                    NK_DEBUG, "Found specular texture %s for mesh %s.", specularHash.c_str(), mesh.primHash.c_str());
                neededTextHashes.insert(specularHash);
            }
        }
        Logger::log(NK_INFO, "Found %d text files to extract from Rpkg files.", neededTextHashes.size());
        if (!neededTextHashes.empty()) {
            std::vector neededTextHashesVec(neededTextHashes.begin(), neededTextHashes.end());
            int result = Rpkg::extractResourcesFromRpkgs(neededTextHashesVec, TEXT);
            if (result != 0) {
                Logger::log(NK_ERROR, "Error extracting Text files from Rpkg files.");
            } else {
                Logger::log(NK_INFO, "Finished extracting %d Text files from Rpkg files.", neededTextHashes.size());
            }
        } else {
            Logger::log(NK_INFO, "No text files to extract from Rpkg files.");
        }
    }
    Logger::log(NK_INFO, "Finished extracting %ss from Rpkg files.", meshFileType.c_str());
    doneExtractingAlocsOrPrims = true;
    Menu::updateMenuState();
}

char* SceneMesh::openSetBlenderFileDialog() {
    return FileUtil::openNfdLoadDialog(nullptr, 0);
}

void SceneMesh::finalizeExtractResources() {
    if (errorExtracting) {
        errorBuilding = false;
        startedSceneMeshGeneration = false;
        blenderSceneMeshBuildStarted = false;
        blenderSceneMeshGenerationDone = false;
        errorExtracting = false;
        extractingResources = false;
        SceneExtract& sceneExtract = SceneExtract::getInstance();
        sceneExtract.alsoBuildAll = false;
        sceneExtract.alsoBuildGlb = false;
    }
    if (doneExtractingAlocsOrPrims) {
        doneExtractingAlocsOrPrims = false;
        const Scene& scene = Scene::getInstance();
        const std::string& fileNameString = scene.lastLoadSceneFile;
        extractingResources = false;
        buildSceneMeshFromScene();
        Logger::log(NK_INFO, ("Done loading nav.json file: '" + fileNameString + "'.").c_str());
        errorExtracting = false;
    }
    Menu::updateMenuState();
}

bool SceneMesh::shouldExtractTextures() const {
    return meshTypeForBuild == PRIM && applyTextures && extractTextures;
}

void SceneMesh::finalizeSceneMeshBuild() {
    SceneExtract& sceneExtract = SceneExtract::getInstance();
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    if (blenderSceneMeshGenerationDone) {
        startedSceneMeshGeneration = false;
        glbToLoad = (std::filesystem::u8path(navKitSettings.outputFolder) / generatedGlbName).string();
        loadGlb = !blendFileOnlyBuild;
        lastGlbFileName = (std::filesystem::u8path(navKitSettings.outputFolder) / generatedGlbName).string();
        blenderSceneMeshBuildStarted = false;
        blenderSceneMeshGenerationDone = false;
        sceneExtract.alsoBuildGlb = false;
        blendFileOnlyBuild = false;
    }
    if (errorBuilding) {
        errorBuilding = false;
        startedSceneMeshGeneration = false;
        blenderSceneMeshBuildStarted = false;
        blenderSceneMeshGenerationDone = false;
        glbLoaded = false;
        sceneExtract.alsoBuildAll = false;
        sceneExtract.alsoBuildGlb = false;
        blendFileOnlyBuild = false;
    }
    Menu::updateMenuState();
}

void SceneMesh::copyFile(const std::string& from, const std::string& to, const std::string& filetype) {
    if (from == to) {
        Logger::log(NK_ERROR, "Cannot overwrite current %s file: %s", filetype.c_str(), from.c_str());
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();
    Logger::log(NK_INFO, "Copying %s from '%s' to '%s'...", filetype.c_str(), from.c_str(), to.c_str());

    try {
        std::filesystem::copy(from, to, std::filesystem::copy_options::overwrite_existing);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        Logger::log(NK_INFO, "Finished saving %s in %lld ms.", filetype.c_str(), duration.count());
    } catch (const std::filesystem::filesystem_error& e) {
        Logger::log(NK_ERROR, "Error copying %s file: %s", filetype.c_str(), e.what());
    }
}

void SceneMesh::saveGlbMesh(char* glbToCopy, char* newFileName) {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving GLB to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    backgroundWorker.emplace(&SceneMesh::copyFile, glbToCopy, newFileName, "GLB");
}

void SceneMesh::saveBlendMesh(std::string blendToCopy, std::string newFileName) {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Saving Blend to file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    backgroundWorker.emplace(&SceneMesh::copyFile, blendToCopy, newFileName, "Blend");
}

void SceneMesh::loadGlbMesh() {
    const std::time_t start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string msg = "Loading GLB from file at ";
    msg += std::ctime(&start_time);
    Logger::log(NK_INFO, msg.data());
    const auto start = std::chrono::high_resolution_clock::now();

    auto recastFuture = std::async(std::launch::async, [this]() {
        Logger::log(NK_INFO, "Loading GLB model data to Recast...");
        const RecastAdapter& recastAdapter = RecastAdapter::getInstance();
        const int result = recastAdapter.loadInputGeom(glbToLoad) && recastAdapter.getVertCount() != 0;
        Logger::log(NK_INFO, "Done loading GLB model data to Recast.");
        return result;
    });

    const auto modelFuture = std::async(std::launch::async, [this]() {
        Logger::log(NK_INFO, "Loading GLB model data to rendering system...");
        model.loadModelData(glbToLoad);
        Logger::log(NK_INFO, "Done loading GLB model data to rendering system.");
    });

    if (recastFuture.get()) {
        modelFuture.wait();
        if (glbLoadDone.empty()) {
            glbLoadDone.push_back(true);
            // Disabling for now. Maybe would be good to add a button to resize the scene bbox to the GLB.
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
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
            msg = "Finished loading GLB in ";
            msg += std::to_string(duration.count());
            msg += " seconds";
            Logger::log(NK_INFO, msg.data());
        }
    } else {
        modelFuture.wait();
        Logger::log(NK_ERROR, "Error loading GLB. It may be invalid, too large, or contain no triangles.");
        model.meshes.clear();
        SceneExtract::getInstance().alsoBuildAll = false;
        Menu::updateMenuState();
    }
}

void SceneMesh::renderGlb() const {
    const Renderer& renderer = Renderer::getInstance();

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

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
    renderer.shader.setVec4("flatColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    model.draw(renderer.shader, renderer.projection * renderer.view);
}

void SceneMesh::renderGlbUsingRecast() {
    RecastAdapter::getInstance().drawInputGeom();
}

char* SceneMesh::openLoadGlbFileDialog() {
    nfdu8filteritem_t filters[1] = {{"glTF Binary files", "glb"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

char* SceneMesh::openSaveGlbFileDialog() {
    nfdu8filteritem_t filters[1] = {{"glTF Binary files", "glb"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output.glb");
}

char* SceneMesh::openSaveBlendFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Blend files", "blend"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output");
}

void SceneMesh::setLastLoadFileName(const char* fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadGlbName = fileName;
        lastGlbFileName = loadGlbName;
        loadGlbName = loadGlbName.substr(loadGlbName.find_last_of("/\\") + 1);
    }
}

void SceneMesh::setLastSaveFileName(const char* fileName) {
    lastSaveGlbFileName = fileName;
    loadGlbName = loadGlbName.substr(loadGlbName.find_last_of("/\\") + 1);
}

void SceneMesh::handleOpenGlbClicked() {
    if (const char* fileName = openLoadGlbFileDialog()) {
        setLastLoadFileName(fileName);
        glbLoaded = false;
        glbToLoad = fileName;
        loadGlb = true;
        Menu::updateMenuState();
    }
}

void SceneMesh::handleSaveGlbClicked() {
    if (const char* fileName = openSaveGlbFileDialog()) {
        std::filesystem::path outputPath = std::filesystem::u8path(fileName);
        if (outputPath.extension() != ".glb") {
            outputPath.replace_extension(".glb");
        }
        loadGlbName = outputPath.string();
        setLastSaveFileName(loadGlbName.c_str());
        saveGlbMesh(lastGlbFileName.data(), lastSaveGlbFileName.data());
        saveGlbName = loadGlbName;
    }
}

void SceneMesh::handleSaveBlendClicked() {
    if (const char* fileName = openSaveBlendFileDialog()) {
        const std::string fileNameStr = fileName;
        const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
        const std::string blendOutputFileName =
            (std::filesystem::u8path(navKitSettings.outputFolder) / "output.blend").string();
        saveBlendMesh(blendOutputFileName, fileNameStr);
    }
}

bool SceneMesh::canLoad() const {
    return glbToLoad.empty();
}

bool SceneMesh::canBuildGlbFromNavp() {
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const Navp& navp = Navp::getInstance();
    return navKitSettings.outputSet && navp.navpLoaded;
}

bool SceneMesh::canBuildGlbFromScene() const {
    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const Scene& scene = Scene::getInstance();
    return navKitSettings.hitmanSet && navKitSettings.outputSet && !extractingResources && navKitSettings.blenderSet &&
        scene.sceneLoaded && !blenderSceneMeshBuildStarted && !blenderSceneMeshGenerationDone &&
        Rpkg::extractionDataInitComplete;
}

bool SceneMesh::canSaveBlend() const {
    return blendFileBuilt;
}

bool SceneMesh::canBuildBlendFromScene() const {
    return canBuildGlbFromScene();
}

bool SceneMesh::canBuildBlendAndGlbFromScene() const {
    return canBuildGlbFromScene();
}

void SceneMesh::handleBuildGlbFromSceneClicked() {
    backgroundWorker.emplace(&SceneMesh::extractResourcesAndStartSceneMeshBuild, this);
}

void SceneMesh::handleBuildBlendFromSceneClicked() {
    blendFileOnlyBuild = true;
    backgroundWorker.emplace(&SceneMesh::extractResourcesAndStartSceneMeshBuild, this);
}

void SceneMesh::handleBuildBlendAndGlbFromSceneClicked() {
    blendFileAndGlbBuild = true;
    backgroundWorker.emplace(&SceneMesh::extractResourcesAndStartSceneMeshBuild, this);
}

void SceneMesh::handleBuildGlbFromNavpClicked() {
    return buildGlbFromNavp(true);
}

void SceneMesh::finalizeLoad() {
    if (loadGlb) {
        lastGlbFileName = glbToLoad;
        RecastAdapter& recastAdapter = RecastAdapter::getInstance();
        recastAdapter.selectedObject = "";
        recastAdapter.markerPositionSet = false;
        std::string msg = "Loading GLB file: '";
        msg += glbToLoad;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        backgroundWorker.emplace(&SceneMesh::loadGlbMesh, this);
        loadGlb = false;
    }

    if (!glbLoadDone.empty()) {
        if (const RecastAdapter& recastAdapter = RecastAdapter::getInstance(); recastAdapter.inputGeom) {
            Logger::log(NK_INFO, "Creating OpenGL buffers for model...");
            loadTileTexture();
            model.initGl();
            Logger::log(NK_INFO, "Finished creating OpenGL buffers.");

            recastAdapter.handleMeshChanged();
            Navp::updateExclusionBoxConvexVolumes();
        }
        glbLoaded = true;
        glbLoadDone.clear();
        glbToLoad.clear();
        Menu::updateMenuState();
        Logger::log(NK_INFO, "GLB load complete.");
        if (Navp& navp = Navp::getInstance(); SceneExtract::getInstance().alsoBuildAll && navp.canBuildNavp()) {
            Logger::log(NK_INFO, "Building Navp...");
            navp.handleBuildNavpClicked();
        }
    }
}

std::string SceneMesh::buildPrimLodsString() const {
    std::string primLodsStr;
    for (const bool primLod : primLods) {
        primLodsStr += primLod ? '1' : '0';
    }
    return primLodsStr;
}

void SceneMesh::loadSettings() {
    const PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    meshTypeForBuild = strcmp(persistedSettings.getValue("Obj", "meshTypeForBuild", "ALOC"), "ALOC") == 0 ? ALOC : PRIM;
    const std::string primLodsStr = persistedSettings.getValue("Obj", "primLods", "11111111");
    for (int i = 0; i < 8; ++i) {
        if (i < primLodsStr.size()) {
            primLods[i] = primLodsStr[i] == '1';
        } else {
            primLods[i] = true;
        }
    }
    sceneMeshBuildType =
        strcmp(persistedSettings.getValue("Obj", "sceneMeshBuildType", "COPY"), "COPY") == 0 ? COPY : INSTANCE;
    skipExtractingAlocsOrPrims =
        strcmp(persistedSettings.getValue("Obj", "skipExtractingAlocsOrPrims", "false"), "true") == 0;
    filterToIncludeBox = strcmp(persistedSettings.getValue("Obj", "filterToIncludeBox", "true"), "true") == 0;
    extractTextures = strcmp(persistedSettings.getValue("Obj", "extractTextures", "false"), "true") == 0;
    applyTextures = strcmp(persistedSettings.getValue("Obj", "applyTextures", "false"), "true") == 0;
}

void SceneMesh::saveSceneMeshSettings() const {
    PersistedSettings& persistedSettings = PersistedSettings::getInstance();

    const char* meshTypeStr = (meshTypeForBuild == PRIM) ? "PRIM" : "ALOC";
    persistedSettings.setValue("Obj", "meshTypeForBuild", meshTypeStr);

    const std::string primLodsStr = buildPrimLodsString();
    persistedSettings.setValue("Obj", "primLods", primLodsStr);

    const char* buildTypeStr = (sceneMeshBuildType == COPY) ? "COPY" : "INSTANCE";
    persistedSettings.setValue("Obj", "sceneMeshBuildType", buildTypeStr);

    const char* skipRpkgExtract = skipExtractingAlocsOrPrims ? "true" : "false";
    persistedSettings.setValue("Obj", "skipExtractingAlocsOrPrims", skipRpkgExtract);

    const char* filterEnabled = filterToIncludeBox ? "true" : "false";
    persistedSettings.setValue("Obj", "filterToIncludeBox", filterEnabled);

    const char* onlyCollidableEnabled = onlyCollidable ? "true" : "false";
    persistedSettings.setValue("Obj", "onlyCollidable", onlyCollidableEnabled);

    const char* extractTexturesEnabled = extractTextures ? "true" : "false";
    persistedSettings.setValue("Obj", "extractTextures", extractTexturesEnabled);

    const char* applyTexturesEnabled = applyTextures ? "true" : "false";
    persistedSettings.setValue("Obj", "applyTextures", applyTexturesEnabled);

    persistedSettings.save();
}

void SceneMesh::resetDefaults() {
    meshTypeForBuild = ALOC;
    for (bool& primLod : primLods) {
        primLod = true;
    }
    sceneMeshBuildType = COPY;
    skipExtractingAlocsOrPrims = false;
    filterToIncludeBox = true;
    onlyCollidable = true;
}
