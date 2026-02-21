#include "../../include/NavKit/module/Scene.h"
#include <CommCtrl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/util/FileUtil.h"

Scene::Scene()
    : sceneLoaded(false),
      showBBox(true),
      showAxes(true),
      version(1),
      loadSceneName("Load NavKit Scene"),
      saveSceneName("Save NavKit Scene") {
    resetBBoxDefaults();
}

Scene::~Scene() {}

HWND Scene::hSceneDialog = nullptr;

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

void Scene::loadMeshes(const std::function<void()>& errorCallback,
                       simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Json::Meshes newMeshes;
    try {
        newMeshes = Json::Meshes(jsonDocument["meshes"]);
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    meshes = newMeshes.readMeshes();
}

void Scene::loadPfBoxes(const std::function<void()>& errorCallback,
                        simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Json::PfBoxes pfBoxes;
    try {
        pfBoxes = Json::PfBoxes(jsonDocument["pfBoxes"]);
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    pfBoxes.readPathfindingBBoxes();
    if (includeBox.id == Json::PfBoxes::NO_INCLUDE_BOX_FOUND) {
        if (Obj::getInstance().objLoaded) {
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
        version = static_cast<int>(static_cast<uint64_t>(jsonDocument["version"]));
    } catch (...) {
        Logger::log(NK_INFO, "Version field not found in scene, defaulting to version zero.");
        version = 0;
    }
}

void Scene::loadPfSeedPoints(const std::function<void()>& errorCallback,
                             simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    Json::PfSeedPoints newPfSeedPoints;
    try {
        newPfSeedPoints = Json::PfSeedPoints(jsonDocument["pfSeedPoints"]);
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, e.what());
    } catch (...) {
        errorCallback();
    }
    pfSeedPoints = newPfSeedPoints.readPfSeedPoints();
}

void Scene::loadRoomsAndVolumes(const std::function<void()>& errorCallback,
                                simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    try {
        Logger::log(NK_INFO, "Loading Gates.");
        gates = Json::Gates(jsonDocument["gates"]).gates;
        Logger::log(NK_INFO, "Loading Rooms.");
        rooms = Json::Rooms(jsonDocument["rooms"]).rooms;
        Logger::log(NK_INFO, "Loading Ai Area Worlds.");
        aiAreaWorlds = Json::AiAreaWorlds(jsonDocument["aiAreaWorld"]).aiAreaWorlds;
        Logger::log(NK_INFO, "Loading Ai Areas.");
        aiAreas = Json::AiAreas(jsonDocument["aiArea"]).aiAreas;
        Logger::log(NK_INFO, "Loading Volume Boxes.");
        volumeBoxes = Json::VolumeBoxes(jsonDocument["volumeBoxes"]).volumeBoxes;
        Logger::log(NK_INFO, "Loading Volume Spheres.");
        volumeSpheres = Json::VolumeSpheres(jsonDocument["volumeSpheres"]).volumeSpheres;
    } catch (const std::exception& e) {
        Logger::log(NK_ERROR, "Error loading scene: %s", e.what());
    } catch (...) {
        errorCallback();
    }
}

void Scene::loadMatis(const std::function<void()>& errorCallback,
                      simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    try {
        for (const auto matiVec = Json::Matis(jsonDocument["matis"]).matis;
             auto mati : matiVec) {
            matis[mati.hash] = mati;
        }
    } catch (const std::exception& e) {
        Logger::log(NK_INFO, "No Matis found in scene file.");
    } catch (...) {
        errorCallback();
    }
}

void Scene::loadPrimMatis(const std::function<void()>& errorCallback,
                          simdjson::simdjson_result<simdjson::ondemand::document>& jsonDocument) {
    try {
        for (const auto primMati : Json::PrimMatis(jsonDocument["primMatis"]).primMatis) {
            primMatis[primMati.primHash] = primMati;
        }
    } catch (const std::exception& e) {
        Logger::log(NK_INFO, "No Prim Matis found in scene file.");
    } catch (...) {
        errorCallback();
    }
}

void Scene::loadScene(const std::string& fileName, const std::function<void()>& callback,
                      const std::function<void()>& errorCallback) {
    sceneLoaded = false;
    Menu::updateMenuState();

    simdjson::ondemand::parser parser;
    const simdjson::padded_string json = simdjson::padded_string::load(fileName);
    auto jsonDocument = parser.iterate(json);

    loadVersion(jsonDocument);
    loadMeshes(errorCallback, jsonDocument);
    loadPfBoxes(errorCallback, jsonDocument);
    loadPfSeedPoints(errorCallback, jsonDocument);
    loadRoomsAndVolumes(errorCallback, jsonDocument);
    loadMatis(errorCallback, jsonDocument);
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
        Logger::log(NK_ERROR, "Failed to open file for saving scene: %s", fileName);
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
            &Scene::loadScene,
            this,
            fileNameToLoad,
            [fileNameToLoad]() {
                getInstance().sceneLoaded = true;
                Menu::updateMenuState();
                Logger::log(
                    NK_INFO,
                    ("Done loading nav.json file: '" + fileNameToLoad + "'.").
                    c_str());
            }, []() {
                Logger::log(NK_ERROR, "Error loading scene file.");
            });
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
        bBoxPos[0] - bBoxScale[0] / 2,
        bBoxPos[1] - bBoxScale[1] / 2,
        bBoxPos[2] - bBoxScale[2] / 2
    };
    const float bBoxMax[3] = {
        bBoxPos[0] + bBoxScale[0] / 2,
        bBoxPos[1] + bBoxScale[1] / 2,
        bBoxPos[2] + bBoxScale[2] / 2
    };
    recastAdapter.setMeshBBox(bBoxMin, bBoxMax);
    Logger::log(NK_INFO, "Setting bbox to (%.2f, %.2f, %.2f) (%.2f, %.2f, %.2f)",
                pos[0], pos[1], pos[2], scale[0], scale[1], scale[2]);
}

void Scene::resetBBoxDefaults() {
    const float pos[3] = {0.0f, 0.0f, 0.0f};
    const float scale[3] = {1000.0f, 300.0f, 1000.0f};
    setBBox(pos, scale);
}

void Scene::updateSceneDialogControls(const HWND hDlg) const {
    // Position Sliders (-500 to 500)
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_X), TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_X), TBM_SETPOS, TRUE, static_cast<int>(bBoxPos[0] + 500.0f));
    SetDlgItemText(hDlg, IDC_STATIC_BBOX_POS_X_VAL, format_float_scene(bBoxPos[0]).c_str());

    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_Y), TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_Y), TBM_SETPOS, TRUE, static_cast<int>(bBoxPos[1] + 500.0f));
    SetDlgItemText(hDlg, IDC_STATIC_BBOX_POS_Y_VAL, format_float_scene(bBoxPos[1]).c_str());

    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_Z), TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_Z), TBM_SETPOS, TRUE, static_cast<int>(bBoxPos[2] + 500.0f));
    SetDlgItemText(hDlg, IDC_STATIC_BBOX_POS_Z_VAL, format_float_scene(bBoxPos[2]).c_str());

    // Scale Sliders (1 to 800)
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_X), TBM_SETRANGE, TRUE, MAKELONG(1, 800));
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_X), TBM_SETPOS, TRUE, static_cast<int>(bBoxScale[0]));
    SetDlgItemText(hDlg, IDC_STATIC_BBOX_SCALE_X_VAL, format_float_scene(bBoxScale[0]).c_str());

    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_Y), TBM_SETRANGE, TRUE, MAKELONG(1, 800));
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_Y), TBM_SETPOS, TRUE, static_cast<int>(bBoxScale[1]));
    SetDlgItemText(hDlg, IDC_STATIC_BBOX_SCALE_Y_VAL, format_float_scene(bBoxScale[1]).c_str());

    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_Z), TBM_SETRANGE, TRUE, MAKELONG(1, 800));
    SendMessage(GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_Z), TBM_SETPOS, TRUE, static_cast<int>(bBoxScale[2]));
    SetDlgItemText(hDlg, IDC_STATIC_BBOX_SCALE_Z_VAL, format_float_scene(bBoxScale[2]).c_str());
}

const Json::Mesh* Scene::findMeshByHashAndIdAndPos(const std::string& hash, const std::string& id,
                                                   const float* pos) const {
    std::vector<const Json::Mesh*> closestMeshes;
    for (const Json::Mesh& mesh : meshes) {
        if ((mesh.alocHash == hash || mesh.primHash == hash) && mesh.id == id) {
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

INT_PTR CALLBACK Scene::sceneDialogProc(const HWND hDlg, const UINT message, const WPARAM wParam, const LPARAM lParam) {
    Scene& scene = getInstance();

    switch (message) {
    case WM_INITDIALOG:
        scene.updateSceneDialogControls(hDlg);
        return TRUE;

    case WM_HSCROLL: {
        bool changed = false;
        const int pos = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);

        if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_X)) {
            scene.bBoxPos[0] = static_cast<float>(pos) - 500.0f;
            SetDlgItemText(hDlg, IDC_STATIC_BBOX_POS_X_VAL, format_float_scene(scene.bBoxPos[0]).c_str());
            changed = true;
        } else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_Y)) {
            scene.bBoxPos[1] = static_cast<float>(pos) - 500.0f;
            SetDlgItemText(hDlg, IDC_STATIC_BBOX_POS_Y_VAL, format_float_scene(scene.bBoxPos[1]).c_str());
            changed = true;
        } else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_BBOX_POS_Z)) {
            scene.bBoxPos[2] = static_cast<float>(pos) - 500.0f;
            SetDlgItemText(hDlg, IDC_STATIC_BBOX_POS_Z_VAL, format_float_scene(scene.bBoxPos[2]).c_str());
            changed = true;
        } else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_X)) {
            scene.bBoxScale[0] = static_cast<float>(pos);
            SetDlgItemText(hDlg, IDC_STATIC_BBOX_SCALE_X_VAL, format_float_scene(scene.bBoxScale[0]).c_str());
            changed = true;
        } else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_Y)) {
            scene.bBoxScale[1] = static_cast<float>(pos);
            SetDlgItemText(hDlg, IDC_STATIC_BBOX_SCALE_Y_VAL, format_float_scene(scene.bBoxScale[1]).c_str());
            changed = true;
        } else if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDER_BBOX_SCALE_Z)) {
            scene.bBoxScale[2] = static_cast<float>(pos);
            SetDlgItemText(hDlg, IDC_STATIC_BBOX_SCALE_Z_VAL, format_float_scene(scene.bBoxScale[2]).c_str());
            changed = true;
        }

        if (changed) {
            scene.setBBox(scene.bBoxPos, scene.bBoxScale);
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BUTTON_RESET_DEFAULTS) {
            scene.resetBBoxDefaults();
            scene.updateSceneDialogControls(hDlg);
        }
        return TRUE;

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return TRUE;

    case WM_DESTROY:
        hSceneDialog = nullptr;
        return TRUE;
    default: ;
    }
    return FALSE;
}

void Scene::showSceneDialog() {
    if (hSceneDialog) {
        SetForegroundWindow(hSceneDialog);
        return;
    }

    const HINSTANCE hInstance = GetModuleHandle(nullptr);
    const HWND hParentWnd = Renderer::hwnd;

    hSceneDialog = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_SCENE_MENU), hParentWnd, sceneDialogProc,
                                     reinterpret_cast<LPARAM>(this));

    if (hSceneDialog) {
        if (HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON))) {
            SendMessage(hSceneDialog, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
            SendMessage(hSceneDialog, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
        }
        RECT parentRect, dialogRect;
        GetWindowRect(hParentWnd, &parentRect);
        GetWindowRect(hSceneDialog, &dialogRect);
        const int parentWidth = parentRect.right - parentRect.left;
        const int parentHeight = parentRect.bottom - parentRect.top;
        const int dialogWidth = dialogRect.right - dialogRect.left;
        const int dialogHeight = dialogRect.bottom - dialogRect.top;
        const int newX = parentRect.left + (parentWidth - dialogWidth) / 2;
        const int newY = parentRect.top + (parentHeight - dialogHeight) / 2;
        SetWindowPos(hSceneDialog, nullptr, newX, newY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        ShowWindow(hSceneDialog, SW_SHOW);
    }
}
