#include "../../include/NavKit/module/Scene.h"
#include <fstream>
#include <functional>
#include <iomanip>
#include <array>
#include <sstream>
#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>

Scene::Scene() :
    sceneLoaded(false), showBBox(true), showAxes(true), version(2), loadSceneName("Load NavKit Scene"),
    saveSceneName("Save NavKit Scene") {
    resetBBoxDefaults();
}

Scene::~Scene() {}

wxDialog* Scene::hSceneDialog = nullptr;

static std::string format_float_scene(const float val) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    return ss.str();
}

char* Scene::openLoadSceneFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Nav.json files", "nav.json"}};
    return FileUtil::openNfdLoadDialog(filters, 1);
}

char* Scene::openSaveSceneFileDialog() {
    nfdu8filteritem_t filters[1] = {{"Nav.json files", "nav.json"}};
    return FileUtil::openNfdSaveDialog(filters, 1, "output");
}

void Scene::setLastLoadFileName(char* fileName) {
    if (std::filesystem::exists(fileName) && !std::filesystem::is_directory(fileName)) {
        loadSceneName = fileName;
        lastLoadSceneFile = loadSceneName.data();
        loadSceneName = loadSceneName.substr(loadSceneName.find_last_of("/\\") + 1);
    }
}

void Scene::setLastSaveFileName(char* fileName) {
    saveSceneName = fileName;
    saveSceneName = saveSceneName.substr(saveSceneName.find_last_of("/\\") + 1);
}

void Scene::loadMeshes(
    const std::function<void()>& errorCallback, simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Json::Meshes newMeshes;
    try {
        Logger::log(NK_INFO, "Loading meshes.");
        newMeshes = Json::Meshes(jsonDocument["meshes"].get_array());
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    meshes = newMeshes.meshes;
}

void Scene::loadPfBoxes(
    const std::function<void()>& errorCallback, simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Json::PfBoxes pfBoxes;
    try {
        Logger::log(NK_INFO, "Loading Pathfinding boxes.");
        pfBoxes = Json::PfBoxes(jsonDocument["pfBoxes"].get_array());
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    pfBoxes.readPathfindingBBoxes();
    if (includeBox.id == Json::PfBoxes::NO_INCLUDE_BOX_FOUND) {
        if (SceneMesh::getInstance().glbLoaded) {
            RecastAdapter::getInstance().setSceneBBoxToMesh();
        }
    } else {
        // Swap Y and Z to go from Hitman's Z+ = Up coordinates to Recast's Y+ = Up coordinates
        // Negate Y position to go from Hitman's Z+ = North to Recast's Y- = North
        const float pos[3] = {includeBox.pos.x, includeBox.pos.z, -includeBox.pos.y};
        const float size[3] = {includeBox.scale.x, includeBox.scale.z, includeBox.scale.y};
        setBBox(pos, size);
    }
}

void Scene::loadVersion(simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    try {
        Logger::log(NK_INFO, "Checking NavKit scene version.");
        version = static_cast<int>(static_cast<uint64_t>(jsonDocument["version"]));
        Logger::log(NK_INFO, "NavKit scene version: %d", version);
    } catch (...) {
        Logger::log(NK_INFO, "Version field not found in scene, defaulting to version zero.");
        version = 0;
    }
}

void Scene::loadPfSeedPoints(
    const std::function<void()>& errorCallback, simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Json::PfSeedPoints newPfSeedPoints;
    try {
        Logger::log(NK_INFO, "Loading Pathfinding seed points.");
        newPfSeedPoints = Json::PfSeedPoints(jsonDocument["pfSeedPoints"].get_array());
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    pfSeedPoints = newPfSeedPoints.readPfSeedPoints();
}

void Scene::loadRoomsAndVolumes(
    const std::function<void()>& errorCallback, simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    try {
        Logger::log(NK_INFO, "Loading Gates.");
        gates = Json::Gates(jsonDocument["gates"].get_array()).gates;
        Logger::log(NK_INFO, "Loading Rooms.");
        rooms = Json::Rooms(jsonDocument["rooms"].get_array()).rooms;
        Logger::log(NK_INFO, "Loading AI Area Worlds.");
        aiAreaWorlds = Json::AiAreaWorlds(jsonDocument["aiAreaWorld"].get_array()).aiAreaWorlds;
        Logger::log(NK_INFO, "Loading AI Areas.");
        aiAreas = Json::AiAreas(jsonDocument["aiArea"].get_array()).aiAreas;
        Logger::log(NK_INFO, "Loading Volume Boxes.");
        volumeBoxes = Json::VolumeBoxes(jsonDocument["volumeBoxes"].get_array()).volumeBoxes;
        Logger::log(NK_INFO, "Loading Volume Spheres.");
        volumeSpheres = Json::VolumeSpheres(jsonDocument["volumeSpheres"].get_array()).volumeSpheres;
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, "Error loading scene: %s", e.what());
    } catch (...) {
        errorCallback();
    }
}

void Scene::loadMatis(
    const std::function<void()>& errorCallback, simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Logger::log(NK_INFO, "Loading Matis.");
    try {
        for (const auto matiVec = Json::Matis(jsonDocument["matis"].get_array()).matis; const auto& mati : matiVec) {
            matis[mati.hash] = mati;
        }
    } catch (const std::exception& e) {
        Logger::log(NK_INFO, "No Matis found in scene file.");
    } catch (...) {
        errorCallback();
    }
}

void Scene::loadPrimMatis(
    const std::function<void()>& errorCallback, simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Logger::log(NK_INFO, "Loading Prim Matis.");
    try {
        for (const auto& primMati : Json::PrimMatis(jsonDocument["primMatis"].get_array()).primMatis) {
            primMatis[primMati.primHash] = primMati;
        }
    } catch (const std::exception& e) {
        Logger::log(NK_INFO, "No Prim Matis found in scene file.");
    } catch (...) {
        errorCallback();
    }
}

void Scene::loadScene(
    const std::string& fileName, const std::function<void()>& callback, const std::function<void()>& errorCallback) {
    sceneLoaded = false;
    Menu::updateMenuState();

    simdjson::ondemand::parser parser;
    const simdjson::padded_string json = simdjson::padded_string::load(fileName);
    auto jsonDocument = parser.iterate(json);

    loadVersion(jsonDocument);
    loadMeshes(errorCallback, jsonDocument);
    loadPfBoxes(errorCallback, jsonDocument);
    loadPfSeedPoints(errorCallback, jsonDocument);
    loadMatis(errorCallback, jsonDocument);
    loadRoomsAndVolumes(errorCallback, jsonDocument);
    loadPrimMatis(errorCallback, jsonDocument);

    callback();
}

void Scene::saveScene(const std::string& fileName) const {
    std::stringstream ss;

    ss << R"({"version":)" << version << ",";
    ss << R"("meshes":[)";
    auto separator = "";
    for (const auto& mesh : meshes) {
        ss << separator;
        mesh.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"pfBoxes":[)";
    if (includeBox.id == Json::PfBoxes::NO_INCLUDE_BOX_FOUND) {
        separator = "";
    } else {
        includeBox.writeJson(ss);
        separator = ",";
    }
    for (const auto& pfBox : exclusionBoxes) {
        ss << separator;
        separator = ",";
        pfBox.writeJson(ss);
    }

    ss << R"(],"pfSeedPoints":[)";
    separator = "";
    for (auto& pfSeedPoint : pfSeedPoints) {
        ss << separator;
        pfSeedPoint.writeJson(ss);
        separator = ",";
    }
    ss << R"(],"matis":[)";
    separator = "";
    for (const auto& mati : matis | std::views::values) {
        ss << separator;
        mati.writeJson(ss);
        separator = ",";
    }
    ss << R"(],"primMatis":[)";
    separator = "";
    for (const auto& primMati : primMatis | std::views::values) {
        ss << separator;
        primMati.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"gates":[)";
    separator = "";
    for (auto& gate : gates) {
        ss << separator;
        gate.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"rooms":[)";
    separator = "";
    for (auto& room : rooms) {
        ss << separator;
        room.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"aiAreaWorld":[)";
    separator = "";
    for (auto& aiAreaWorld : aiAreaWorlds) {
        ss << separator;
        aiAreaWorld.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"aiArea":[)";
    separator = "";
    for (auto& aiArea : aiAreas) {
        ss << separator;
        aiArea.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"volumeBoxes":[)";
    separator = "";
    for (auto& volumeBox : volumeBoxes) {
        ss << separator;
        volumeBox.writeJson(ss);
        separator = ",";
    }

    ss << R"(],"volumeSpheres":[)";
    separator = "";
    for (auto& volumeSphere : volumeSpheres) {
        ss << separator;
        volumeSphere.writeJson(ss);
        separator = ",";
    }
    ss << "]}";

    std::ofstream f(fileName);
    if (f.is_open()) {
        f << ss.rdbuf();
        f.close();
        Logger::log(NK_INFO, "Done saving Scene.");
    } else {
        Logger::log(NK_ERROR, "Failed to open file for saving scene: %s", fileName.c_str());
    }
}

void Scene::handleOpenSceneClicked() {
    if (char* fileName = openLoadSceneFileDialog()) {
        setLastLoadFileName(fileName);
        std::string msg = "Loading nav.json file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        std::string fileNameToLoad = fileName;
        backgroundWorker.emplace(
            &Scene::loadScene, this, fileNameToLoad,
            [fileNameToLoad]() {
                getInstance().sceneLoaded = true;
                Menu::updateMenuState();
                Logger::log(NK_INFO, ("Done loading nav.json file: '" + fileNameToLoad + "'.").c_str());
            },
            []() { Logger::log(NK_ERROR, "Error loading scene file."); });
    }
}

void Scene::handleSaveSceneClicked() {
    if (char* fileName = openSaveSceneFileDialog()) {
        std::string fileNameStr = fileName;
        setLastSaveFileName(fileName);
        std::string msg = "Saving NavKit Scene file: '";
        msg += fileName;
        msg += "'...";
        Logger::log(NK_INFO, msg.data());
        backgroundWorker.emplace(&Scene::saveScene, this, fileNameStr);
    }
}

void Scene::setBBox(const float* pos, const float* scale) {
    bBoxPos[0] = pos[0];
    bBoxPos[1] = pos[1];
    bBoxPos[2] = pos[2];
    bBoxScale[0] = scale[0];
    bBoxScale[1] = scale[1];
    bBoxScale[2] = scale[2];

    const RecastAdapter& recastAdapter = RecastAdapter::getInstance();
    const float bBoxMin[3] = {
        bBoxPos[0] - bBoxScale[0] / 2, bBoxPos[1] - bBoxScale[1] / 2, bBoxPos[2] - bBoxScale[2] / 2};
    const float bBoxMax[3] = {
        bBoxPos[0] + bBoxScale[0] / 2, bBoxPos[1] + bBoxScale[1] / 2, bBoxPos[2] + bBoxScale[2] / 2};
    recastAdapter.setMeshBBox(bBoxMin, bBoxMax);
    Logger::log(NK_INFO, "Setting bbox to (%.2f, %.2f, %.2f) (%.2f, %.2f, %.2f)", pos[0], pos[1], pos[2], scale[0],
        scale[1], scale[2]);
}

void Scene::resetBBoxDefaults() {
    const float pos[3] = {0.0f, 0.0f, 0.0f};
    const float scale[3] = {1000.0f, 300.0f, 1000.0f};
    setBBox(pos, scale);
}

void Scene::updateSceneDialogControls(wxDialog* dialog) const {
    const std::array<int, 6> sliders = {IDC_SLIDER_BBOX_POS_X, IDC_SLIDER_BBOX_POS_Y, IDC_SLIDER_BBOX_POS_Z,
        IDC_SLIDER_BBOX_SCALE_X, IDC_SLIDER_BBOX_SCALE_Y, IDC_SLIDER_BBOX_SCALE_Z};
    const std::array<int, 6> labels = {IDC_STATIC_BBOX_POS_X_VAL, IDC_STATIC_BBOX_POS_Y_VAL, IDC_STATIC_BBOX_POS_Z_VAL,
        IDC_STATIC_BBOX_SCALE_X_VAL, IDC_STATIC_BBOX_SCALE_Y_VAL, IDC_STATIC_BBOX_SCALE_Z_VAL};
    for (int axis = 0; axis < 3; ++axis) {
        auto* position = static_cast<wxSlider*>(dialog->FindWindow(sliders[axis]));
        position->SetRange(0, 1000);
        position->SetValue(static_cast<int>(bBoxPos[axis] + 500.0f));
        dialog->FindWindow(labels[axis])->SetLabel(format_float_scene(bBoxPos[axis]));
        auto* scale = static_cast<wxSlider*>(dialog->FindWindow(sliders[axis + 3]));
        scale->SetRange(1, 800);
        scale->SetValue(static_cast<int>(bBoxScale[axis]));
        dialog->FindWindow(labels[axis + 3])->SetLabel(format_float_scene(bBoxScale[axis]));
    }
}

const Json::Mesh* Scene::findMeshByHashAndIdAndPos(
    const std::string& hash, const std::string& id, const float* pos) const {
    std::vector<const Json::Mesh*> closestMeshes;
    for (const Json::Mesh& mesh : meshes) {
        if ((mesh.alocHash == hash || mesh.primHash == hash) && mesh.entity.id == id) {
            closestMeshes.push_back(&mesh);
        }
    }
    if (closestMeshes.empty()) {
        return nullptr;
    }
    Vec3 posVec = {pos[0], pos[1], pos[2]};
    std::ranges::sort(closestMeshes, [posVec](const Json::Mesh* a, const Json::Mesh* b) {
        const Vec3 aPos = {a->pos.x, a->pos.y, a->pos.z};
        const Vec3 bPos = {b->pos.x, b->pos.y, b->pos.z};
        return posVec.DistanceSquaredTo(aPos) < posVec.DistanceSquaredTo(bPos);
    });
    return closestMeshes[0];
}

void Scene::showSceneDialog() {
    if (hSceneDialog) {
        hSceneDialog->Raise();
        return;
    }
    auto* dialog = new wxDialog(getMainFrame(), wxID_ANY, "Scene Properties", wxDefaultPosition, wxSize(500, 330));
    hSceneDialog = dialog;
    const std::array<int, 6> sliderIds = {IDC_SLIDER_BBOX_POS_X, IDC_SLIDER_BBOX_POS_Y, IDC_SLIDER_BBOX_POS_Z,
        IDC_SLIDER_BBOX_SCALE_X, IDC_SLIDER_BBOX_SCALE_Y, IDC_SLIDER_BBOX_SCALE_Z};
    const std::array<int, 6> labelIds = {IDC_STATIC_BBOX_POS_X_VAL, IDC_STATIC_BBOX_POS_Y_VAL,
        IDC_STATIC_BBOX_POS_Z_VAL, IDC_STATIC_BBOX_SCALE_X_VAL, IDC_STATIC_BBOX_SCALE_Y_VAL,
        IDC_STATIC_BBOX_SCALE_Z_VAL};
    const std::array<const char*, 6> names = {
        "BBox Pos X:", "BBox Pos Y:", "BBox Pos Z:", "BBox Scale X:", "BBox Scale Y:", "BBox Scale Z:"};
    auto* controls = new wxFlexGridSizer(6, 3, 8, 8);
    for (int i = 0; i < 6; ++i) {
        controls->Add(new wxStaticText(dialog, wxID_ANY, names[i]), 0, wxALIGN_CENTER_VERTICAL);
        controls->Add(new wxSlider(dialog, sliderIds[i], 0, 0, 1), 1, wxEXPAND);
        controls->Add(new wxStaticText(dialog, labelIds[i], ""), 0, wxALIGN_CENTER_VERTICAL);
    }
    controls->AddGrowableCol(1, 1);
    auto* reset = new wxButton(dialog, IDC_BUTTON_RESET_DEFAULTS, "Reset Defaults");
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(controls, 1, wxEXPAND | wxALL, 12);
    sizer->Add(reset, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    dialog->SetSizer(sizer);
    updateSceneDialogControls(dialog);
    for (int i = 0; i < 6; ++i) {
        dialog->Bind(
            wxEVT_SLIDER,
            [dialog, i, labelId = labelIds[i]](wxCommandEvent& event) {
                Scene& scene = Scene::getInstance();
                if (i < 3) {
                    scene.bBoxPos[i] = static_cast<float>(event.GetInt()) - 500.0f;
                    dialog->FindWindow(labelId)->SetLabel(format_float_scene(scene.bBoxPos[i]));
                } else {
                    scene.bBoxScale[i - 3] = static_cast<float>(event.GetInt());
                    dialog->FindWindow(labelId)->SetLabel(format_float_scene(scene.bBoxScale[i - 3]));
                }
                scene.setBBox(scene.bBoxPos, scene.bBoxScale);
            },
            sliderIds[i]);
    }
    reset->Bind(wxEVT_BUTTON, [dialog](wxCommandEvent&) {
        Scene& scene = Scene::getInstance();
        scene.resetBBoxDefaults();
        scene.updateSceneDialogControls(dialog);
    });
    dialog->Bind(wxEVT_CLOSE_WINDOW, [dialog](wxCloseEvent&) {
        if (hSceneDialog == dialog) {
            hSceneDialog = nullptr;
        }
        dialog->Destroy();
    });
    dialog->Layout();
    dialog->CentreOnParent();
    dialog->Show();
}
