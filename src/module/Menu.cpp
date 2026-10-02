#include "../../include/NavKit/module/Menu.h"
#include <SDL.h>
#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/UiIds.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Rpkg.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/WxApplication.h"
#include <wx/msgdlg.h>
#include <wx/thread.h>

wxMenuBar* Menu::createMenuBar() {
    auto* bar = new wxMenuBar();
    auto* file = new wxMenu();
    file->Append(IDM_FILE_OPEN_SCENE, "Open &Scene");
    file->Append(IDM_FILE_OPEN_OBJ, "Open &Obj");
    file->Append(IDM_FILE_OPEN_NAVP, "Open &Navp");
    file->Append(IDM_FILE_OPEN_NAVP_FROM_RPKG, "Open Navp from &Rpkg");
    file->Append(IDM_FILE_OPEN_AIRG, "Open &Airg");
    file->Append(IDM_FILE_OPEN_AIRG_FROM_RPKG, "Open Airg from &Rpkg");
    file->AppendSeparator();
    file->Append(IDM_FILE_SAVE_SCENE, "Save &Scene");
    file->Append(IDM_FILE_SAVE_OBJ, "Save &Obj and Mtl");
    file->Append(IDM_FILE_SAVE_BLEND, "Save &Blend");
    file->Append(IDM_FILE_SAVE_NAVP, "Save &Navp");
    file->Append(IDM_FILE_SAVE_AIRG, "Save &Airg");
    file->AppendSeparator();
    file->Append(IDM_FILE_EXIT, "E&xit");
    bar->Append(file, "&File");

    auto* edit = new wxMenu();
    edit->AppendCheckItem(IDM_EDIT_NAVP_STAIRS, "&Stairs Area");
    edit->AppendCheckItem(IDM_EDIT_AIRG_CONNECT_WAYPOINT, "&Connect Airg Waypoint Mode");
    edit->AppendCheckItem(IDM_EDIT_AIRG_DISCONNECT_WAYPOINT, "&Disconnect Airg Waypoint Mode");
    bar->Append(edit, "&Edit");

    auto* settings = new wxMenu();
    settings->Append(IDM_SETTINGS_NAVKIT, "&NavKit Settings");
    settings->Append(IDM_SETTINGS_SCENE, "&Scene Settings");
    settings->Append(IDM_SETTINGS_RECAST, "&Recast Settings");
    settings->Append(IDM_SETTINGS_SCENE_MESH, "&Scene Mesh Settings");
    settings->Append(IDM_SETTINGS_AIRG, "&Airg Settings");
    bar->Append(settings, "&Settings");

    auto* view = new wxMenu();
    view->AppendCheckItem(IDM_VIEW_SCENE_SHOW_BBOX, "Show &Bounding Box")->Check();
    view->AppendCheckItem(IDM_VIEW_SCENE_SHOW_AXES, "Show &Axes")->Check();
    view->AppendSeparator();
    view->AppendCheckItem(IDM_VIEW_NAVP_SHOW_NAVP, "Show &Navp")->Check();
    view->AppendCheckItem(IDM_VIEW_NAVP_SHOW_INDICES, "Show Navp &Indices");
    view->AppendCheckItem(IDM_VIEW_NAVP_SHOW_KD_TREE, "Show &KD Tree");
    view->AppendCheckItem(IDM_VIEW_NAVP_SHOW_PF_EXCLUDE_BOXES, "Show &Exclusion Boxes")->Check();
    view->AppendCheckItem(IDM_VIEW_NAVP_SHOW_PF_SEED_POINTS, "Show &Seed Points")->Check();
    view->AppendCheckItem(IDM_VIEW_NAVP_Z_RENDER_OFFSET, "&Z Render Offset");
    view->AppendCheckItem(IDM_VIEW_NAVP_SHOW_RECAST_DEBUG_INFO, "Show &Recast Debug Info");
    view->AppendSeparator();
    view->AppendCheckItem(IDM_VIEW_AIRG_SHOW_AIRG, "Show &Airg")->Check();
    view->AppendCheckItem(IDM_VIEW_AIRG_SHOW_INDICES, "Show Airg &Indices");
    view->AppendCheckItem(IDM_VIEW_AIRG_SHOW_GRID, "Show &Grid")->Check();
    view->AppendCheckItem(IDM_VIEW_AIRG_SHOW_RECAST_DEBUG_INFO, "Show &Recast Debug Info");
    view->AppendSeparator();
    view->AppendRadioItem(IDM_VIEW_AIRG_CELL_COLOR_OFF, "Airg Cell: None")->Check();
    view->AppendRadioItem(IDM_VIEW_AIRG_CELL_COLOR_BITMAP, "Airg Cell: Area Bitmap");
    view->AppendRadioItem(IDM_VIEW_AIRG_CELL_COLOR_VISION_DATA, "Airg Cell: Vision Data");
    view->AppendRadioItem(IDM_VIEW_AIRG_CELL_COLOR_LAYER, "Airg Cell: Layer");
    view->AppendSeparator();
    view->AppendCheckItem(IDM_VIEW_OBJ_SHOW_OBJ, "Show &Obj")->Check();
    view->AppendSeparator();
    view->AppendCheckItem(IDM_VIEW_LOG_SHOW_LOG, "Show &Log")->Check();
    bar->Append(view, "&View");

    auto* extract = new wxMenu();
    extract->Append(IDM_EXTRACT_SCENE, "Extract &Scene from game");
    extract->Append(IDM_EXTRACT_SCENE_AND_BUILD_OBJ, "Extract Scene from game and build &Obj");
    extract->Append(IDM_EXTRACT_SCENE_AND_BUILD_ALL, "Extract Scene, build &navp and airg");
    bar->Append(extract, "&Extract");

    auto* build = new wxMenu();
    build->Append(IDM_BUILD_OBJ_FROM_SCENE, "Build Obj from &Scene");
    build->Append(IDM_BUILD_OBJ_FROM_NAVP, "Build Obj from &Navp");
    build->Append(IDM_BUILD_BLEND_AND_OBJ_FROM_SCENE, "Build Obj and Blend from &Scene");
    build->Append(IDM_BUILD_BLEND_FROM_SCENE, "Build Blend from &Scene");
    build->AppendSeparator();
    build->Append(IDM_BUILD_NAVP, "Build &Navp from Obj and Scene");
    build->AppendSeparator();
    build->Append(IDM_BUILD_AIRG, "Build &Airg from Navp");
    bar->Append(build, "&Build");

    auto* help = new wxMenu();
    help->Append(IDM_HELP_ABOUT, "&About");
    bar->Append(help, "&Help");
    return bar;
}

void Menu::setMenuItemEnabled(const int menuId, const bool isEnabled) {
    if (wxFrame* frame = getMainFrame(); frame && frame->GetMenuBar()) {
        frame->GetMenuBar()->Enable(menuId, isEnabled);
    }
}

void Menu::handleCheckboxMenuItem(const int menuId, bool& stateVariable, const char* itemName) {
    stateVariable = !stateVariable;
    if (wxFrame* frame = getMainFrame(); frame && frame->GetMenuBar()) {
        frame->GetMenuBar()->Check(menuId, stateVariable);
    }
    Logger::log(NK_DEBUG, ("Toggled " + std::string(itemName) + " " + (stateVariable ? "ON" : "OFF")).data());
}

void Menu::handleCellColorDataRadioMenuItem(const int selectedMenuId) {
    const std::vector<int> menuGroupIds = {IDM_VIEW_AIRG_CELL_COLOR_OFF, IDM_VIEW_AIRG_CELL_COLOR_BITMAP,
        IDM_VIEW_AIRG_CELL_COLOR_VISION_DATA, IDM_VIEW_AIRG_CELL_COLOR_LAYER};
    if (wxFrame* frame = getMainFrame(); frame && frame->GetMenuBar()) {
        for (const int menuId : menuGroupIds) {
            frame->GetMenuBar()->Check(menuId, selectedMenuId == menuId);
        }
    }
    std::string itemName;
    CellColorDataSource selectedCellColorDataSource;
    switch (selectedMenuId) {
    case IDM_VIEW_AIRG_CELL_COLOR_OFF:
        itemName = "off";
        selectedCellColorDataSource = OFF;
        break;
    case IDM_VIEW_AIRG_CELL_COLOR_BITMAP:
        itemName = "Bitmap";
        selectedCellColorDataSource = AIRG_BITMAP;
        break;
    case IDM_VIEW_AIRG_CELL_COLOR_VISION_DATA:
        itemName = "Vision Data";
        selectedCellColorDataSource = VISION_DATA;
        break;
    case IDM_VIEW_AIRG_CELL_COLOR_LAYER:
        itemName = "Layer";
        selectedCellColorDataSource = LAYER;
        break;
    default:
        return;
    }
    Airg::getInstance().cellColorSource = selectedCellColorDataSource;
    Logger::log(NK_DEBUG, ("Set Cell Color Data Source to " + std::string(itemName)).data());
}

void Menu::updateMenuState() {
    if (!wxIsMainThread()) {
        if (wxTheApp) {
            wxTheApp->CallAfter([] { updateMenuState(); });
        }
        return;
    }
    const Scene& scene = Scene::getInstance();
    const SceneExtract& sceneExtract = SceneExtract::getInstance();
    const Airg& airg = Airg::getInstance();
    const Navp& navp = Navp::getInstance();
    const SceneMesh& obj = SceneMesh::getInstance();
    const bool isSceneLoaded = scene.sceneLoaded;
    const bool isNavpLoaded = navp.navpLoaded;
    const bool isObjLoaded = obj.objLoaded;
    const bool isAreaSelected = isNavpLoaded && navp.selectedNavpAreaIndex != -1;

    setMenuItemEnabled(IDM_FILE_OPEN_AIRG, airg.canLoad());
    setMenuItemEnabled(IDM_FILE_OPEN_OBJ, obj.canLoad());
    setMenuItemEnabled(IDM_FILE_OPEN_NAVP_FROM_RPKG, Rpkg::canExtract());
    setMenuItemEnabled(IDM_FILE_OPEN_AIRG_FROM_RPKG, Rpkg::canExtract());
    setMenuItemEnabled(IDM_FILE_SAVE_SCENE, isSceneLoaded);
    setMenuItemEnabled(IDM_FILE_SAVE_NAVP, isNavpLoaded);
    setMenuItemEnabled(IDM_FILE_SAVE_AIRG, airg.canSave());
    setMenuItemEnabled(IDM_FILE_SAVE_OBJ, isObjLoaded);
    setMenuItemEnabled(IDM_FILE_SAVE_BLEND, obj.canSaveBlend());

    setMenuItemEnabled(IDM_EDIT_NAVP_STAIRS, isAreaSelected);
    setMenuItemEnabled(IDM_EDIT_AIRG_CONNECT_WAYPOINT, airg.canEnterConnectWaypointMode());
    setMenuItemEnabled(IDM_EDIT_AIRG_DISCONNECT_WAYPOINT, airg.canEnterConnectWaypointMode());

    setMenuItemEnabled(IDM_BUILD_NAVP, navp.canBuildNavp());
    setMenuItemEnabled(IDM_BUILD_AIRG, airg.canBuildAirg());
    setMenuItemEnabled(IDM_BUILD_OBJ_FROM_SCENE, obj.canBuildObjFromScene());
    setMenuItemEnabled(IDM_BUILD_OBJ_FROM_NAVP, obj.canBuildObjFromNavp());
    setMenuItemEnabled(IDM_BUILD_BLEND_FROM_SCENE, obj.canBuildBlendFromScene());
    setMenuItemEnabled(IDM_BUILD_BLEND_AND_OBJ_FROM_SCENE, obj.canBuildBlendAndObjFromScene());

    setMenuItemEnabled(IDM_EXTRACT_SCENE, sceneExtract.canExtractFromGame());
    setMenuItemEnabled(IDM_EXTRACT_SCENE_AND_BUILD_OBJ, sceneExtract.canExtractFromGameAndBuildObj());
    setMenuItemEnabled(IDM_EXTRACT_SCENE_AND_BUILD_ALL, sceneExtract.canExtractFromGameAndBuildAll());

    bool isStairs = false;
    if (isAreaSelected) {
        isStairs = navp.stairsAreaSelected();
    }
    setMenuItemChecked(IDM_EDIT_NAVP_STAIRS, isStairs, "Stairs Area");
    setMenuItemChecked(
        IDM_EDIT_AIRG_CONNECT_WAYPOINT, airg.connectWaypointModeEnabled, "Connect Waypoint Mode Enabled");
    setMenuItemChecked(
        IDM_EDIT_AIRG_DISCONNECT_WAYPOINT, airg.disconnectWaypointModeEnabled, "Disconnect Waypoint Mode Enabled");
}

void Menu::setMenuItemChecked(const int menuId, const bool isChecked, const char* itemName) {
    if (wxFrame* frame = getMainFrame(); frame && frame->GetMenuBar()) {
        frame->GetMenuBar()->Check(menuId, isChecked);
    }
}

int Menu::handleMenuClicked(const int menuId) {
    Navp& navp = Navp::getInstance();
    SceneMesh& obj = SceneMesh::getInstance();
    Airg& airg = Airg::getInstance();
    SceneExtract& sceneExtract = SceneExtract::getInstance();
    switch (menuId) {
    case IDM_FILE_OPEN_SCENE:
        Scene::getInstance().handleOpenSceneClicked();
        Logger::log(NK_DEBUG, "File -> Open Scene clicked");
        break;
    case IDM_FILE_OPEN_OBJ:
        SceneMesh::getInstance().handleOpenObjClicked();
        Logger::log(NK_DEBUG, "File -> Open Obj clicked");
        break;
    case IDM_FILE_OPEN_NAVP:
        Navp::getInstance().handleOpenNavpClicked();
        Logger::log(NK_DEBUG, "File -> Open Navp clicked");
        break;
    case IDM_FILE_OPEN_NAVP_FROM_RPKG:
        Navp::getInstance().showExtractNavpDialog();
        Logger::log(NK_DEBUG, "File -> Open Navp from Rpkg clicked");
        break;
    case IDM_FILE_OPEN_AIRG:
        Airg::getInstance().handleOpenAirgClicked();
        Logger::log(NK_DEBUG, "File -> Open Airg clicked");
        break;
    case IDM_FILE_OPEN_AIRG_FROM_RPKG:
        Airg::getInstance().showExtractAirgDialog();
        Logger::log(NK_DEBUG, "File -> Open Airg from Rpkg clicked");
        break;
    case IDM_FILE_SAVE_SCENE:
        Scene::getInstance().handleSaveSceneClicked();
        Logger::log(NK_DEBUG, "File -> Save Scene clicked");
        break;
    case IDM_FILE_SAVE_OBJ:
        SceneMesh::getInstance().handleSaveObjClicked();
        Logger::log(NK_DEBUG, "File -> Save Obj and Mtl clicked");
        break;
    case IDM_FILE_SAVE_BLEND:
        SceneMesh::getInstance().handleSaveBlendClicked();
        Logger::log(NK_DEBUG, "File -> Save Blend clicked");
        break;
    case IDM_FILE_SAVE_NAVP:
        Navp::getInstance().handleSaveNavpClicked();
        Logger::log(NK_DEBUG, "File -> Save Navp clicked");
        break;
    case IDM_FILE_SAVE_AIRG:
        Airg::getInstance().handleSaveAirgClicked();
        Logger::log(NK_DEBUG, "File -> Save Airg clicked");
        break;

    case IDM_FILE_EXIT:
        Logger::log(NK_DEBUG, "File -> Exit clicked");
        return InputHandler::QUIT;

    case IDM_EDIT_NAVP_STAIRS:
        Logger::log(NK_DEBUG, "Edit -> Navp Stairs clicked");
        navp.handleEditStairsClicked();
        break;

    case IDM_EDIT_AIRG_CONNECT_WAYPOINT:
        Logger::log(NK_DEBUG, "Edit -> Airg Connect Waypoint clicked");
        airg.handleConnectWaypointClicked();
        break;

    case IDM_EDIT_AIRG_DISCONNECT_WAYPOINT:
        Logger::log(NK_DEBUG, "Edit -> Airg Disconnect Waypoint clicked");
        airg.handleDisconnectWaypointClicked();
        break;

    case IDM_VIEW_SCENE_SHOW_BBOX:
        handleCheckboxMenuItem(IDM_VIEW_SCENE_SHOW_BBOX, Scene::getInstance().showBBox, "Show Bounding Box");
        break;

    case IDM_VIEW_SCENE_SHOW_AXES:
        handleCheckboxMenuItem(IDM_VIEW_SCENE_SHOW_AXES, Scene::getInstance().showAxes, "Show Axes");
        break;

    case IDM_VIEW_NAVP_SHOW_NAVP:
        handleCheckboxMenuItem(IDM_VIEW_NAVP_SHOW_NAVP, Navp::getInstance().showNavp, "Show Navp");
        break;
    case IDM_VIEW_NAVP_SHOW_INDICES:
        handleCheckboxMenuItem(IDM_VIEW_NAVP_SHOW_INDICES, Navp::getInstance().showNavpIndices, "Show Navp Indices");
        break;
    case IDM_VIEW_NAVP_SHOW_KD_TREE:
        handleCheckboxMenuItem(IDM_VIEW_NAVP_SHOW_KD_TREE, Navp::getInstance().showKdTree, "Show KD Tree");
        break;
    case IDM_VIEW_NAVP_SHOW_PF_EXCLUDE_BOXES:
        handleCheckboxMenuItem(
            IDM_VIEW_NAVP_SHOW_PF_EXCLUDE_BOXES, Navp::getInstance().showPfExclusionBoxes, "Show Exclusion Boxes");
        break;
    case IDM_VIEW_NAVP_SHOW_PF_SEED_POINTS:
        handleCheckboxMenuItem(
            IDM_VIEW_NAVP_SHOW_PF_SEED_POINTS, Navp::getInstance().showPfSeedPoints, "Show Seed Points");
        break;
    case IDM_VIEW_NAVP_Z_RENDER_OFFSET:
        handleCheckboxMenuItem(IDM_VIEW_NAVP_Z_RENDER_OFFSET, Navp::getInstance().doZRenderOffset, "Z Render Offset");
        Navp::navMeshDirty = true;
        Navp::hitTestDirty = true;
        break;
    case IDM_VIEW_NAVP_SHOW_RECAST_DEBUG_INFO:
        handleCheckboxMenuItem(
            IDM_VIEW_NAVP_SHOW_RECAST_DEBUG_INFO, Navp::getInstance().showRecastDebugInfo, "Show Recast Debug Info");
        break;
    case IDM_VIEW_OBJ_SHOW_OBJ:
        handleCheckboxMenuItem(IDM_VIEW_OBJ_SHOW_OBJ, SceneMesh::getInstance().showObj, "Show Obj");
        break;
    case IDM_VIEW_AIRG_SHOW_AIRG:
        handleCheckboxMenuItem(IDM_VIEW_AIRG_SHOW_AIRG, Airg::getInstance().showAirg, "Show Airg");
        break;
    case IDM_VIEW_AIRG_SHOW_INDICES:
        handleCheckboxMenuItem(IDM_VIEW_AIRG_SHOW_INDICES, Airg::getInstance().showAirgIndices, "Show Airg Indices");
        break;
    case IDM_VIEW_AIRG_SHOW_GRID:
        handleCheckboxMenuItem(IDM_VIEW_AIRG_SHOW_GRID, Grid::getInstance().showGrid, "Show Grid");
        break;
    case IDM_VIEW_AIRG_SHOW_RECAST_DEBUG_INFO:
        handleCheckboxMenuItem(
            IDM_VIEW_AIRG_SHOW_RECAST_DEBUG_INFO, Airg::getInstance().showRecastDebugInfo, "Show Recast Debug Info");
        break;
    case IDM_VIEW_LOG_SHOW_LOG:
        handleCheckboxMenuItem(IDM_VIEW_LOG_SHOW_LOG, Gui::getInstance().showLog, "Show Log");
        break;
    case IDM_VIEW_AIRG_CELL_COLOR_OFF:
    case IDM_VIEW_AIRG_CELL_COLOR_BITMAP:
    case IDM_VIEW_AIRG_CELL_COLOR_VISION_DATA:
    case IDM_VIEW_AIRG_CELL_COLOR_LAYER:
        handleCellColorDataRadioMenuItem(menuId);
        break;

    case IDM_BUILD_OBJ_FROM_NAVP:
        obj.handleBuildObjFromNavpClicked();
        break;
    case IDM_BUILD_BLEND_FROM_SCENE:
        obj.handleBuildBlendFromSceneClicked();
        break;
    case IDM_BUILD_BLEND_AND_OBJ_FROM_SCENE:
        obj.handleBuildBlendAndObjFromSceneClicked();
        break;
    case IDM_BUILD_OBJ_FROM_SCENE:
        obj.handleBuildObjFromSceneClicked();
        break;
    case IDM_BUILD_NAVP:
        navp.handleBuildNavpClicked();
        break;
    case IDM_BUILD_AIRG:
        airg.handleBuildAirgClicked();
        break;

    case IDM_EXTRACT_SCENE:
        sceneExtract.handleExtractFromGameClicked();
        break;
    case IDM_EXTRACT_SCENE_AND_BUILD_OBJ:
        sceneExtract.handleExtractFromGameAndBuildObjClicked();
        break;
    case IDM_EXTRACT_SCENE_AND_BUILD_ALL:
        sceneExtract.handleExtractFromGameAndBuildAllClicked();
        break;

    case IDM_HELP_ABOUT: {
        const std::string currentVersionStr = std::string(NavKit_VERSION_MAJOR) + "." +
            std::string(NavKit_VERSION_MINOR) + "." + std::string(NavKit_VERSION_PATCH);
        wxMessageBox("NavKit version " + currentVersionStr, "About", wxOK | wxICON_INFORMATION, getMainFrame());
        break;
    }

    case IDM_SETTINGS_NAVKIT:
        NavKitSettings::getInstance().showNavKitSettingsDialog();
        Logger::log(NK_DEBUG, "Settings -> NavKit Settings clicked");
        break;
    case IDM_SETTINGS_SCENE:
        Scene::getInstance().showSceneDialog();
        Logger::log(NK_DEBUG, "Settings -> Scene Settings clicked");
        break;
    case IDM_SETTINGS_AIRG:
        Airg::getInstance().showAirgDialog();
        Logger::log(NK_DEBUG, "Settings -> Airg Settings clicked");
        break;
    case IDM_SETTINGS_RECAST:
        RecastAdapter::getInstance().showRecastDialog();
        Logger::log(NK_DEBUG, "Settings -> Recast Settings clicked");
        break;
    case IDM_SETTINGS_SCENE_MESH:
        SceneMesh::getInstance().showSceneMeshDialog();
        Logger::log(NK_DEBUG, "Settings -> Scene Mesh Settings clicked");
        break;

    default:
        break;
    }
    return 0;
}
