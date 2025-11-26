#include "../../include/NavKit/module/Menu.h"
#include <SDL.h>
#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/SceneExtract.h"
#include "../../include/NavKit/module/Settings.h"


void Menu::setMenuItemEnabled(const UINT menuId, const bool isEnabled) {
    const HWND hwnd = Renderer::hwnd;
    if (!hwnd) return;
    const HMENU hMenu = GetMenu(hwnd);
    if (!hMenu) return;
    const UINT flags = MF_BYCOMMAND | (isEnabled ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, menuId, flags);
}

void Menu::handleCheckboxMenuItem(const UINT menuId, bool &stateVariable, const char *itemName) {
    const HWND hwnd = Renderer::hwnd;
    const HMENU hMenu = GetMenu(hwnd);
    stateVariable = !stateVariable;
    const UINT checkState = stateVariable ? MF_CHECKED : MF_UNCHECKED;
    CheckMenuItem(hMenu, menuId, MF_BYCOMMAND | checkState);
    Logger::log(NK_DEBUG, ("Toggled " + std::string(itemName) + " " + (stateVariable ? "ON" : "OFF")).data());
}

void Menu::handleCellColorDataRadioMenuItem(const int selectedMenuId) {
    const std::vector<UINT> menuGroupIds =
    {
        IDM_VIEW_AIRG_CELL_COLOR_OFF,
        IDM_VIEW_AIRG_CELL_COLOR_BITMAP,
        IDM_VIEW_AIRG_CELL_COLOR_VISION_DATA,
        IDM_VIEW_AIRG_CELL_COLOR_LAYER
    };
    HWND hwnd = Renderer::hwnd;
    HMENU hMenu = GetMenu(hwnd);
    for (const UINT menuId: menuGroupIds) {
        if (selectedMenuId == menuId) {
            CheckMenuItem(hMenu, menuId, MF_BYCOMMAND | MF_CHECKED);
        } else
            CheckMenuItem(hMenu, menuId, MF_BYCOMMAND | MF_UNCHECKED);
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
    const Scene &scene = Scene::getInstance();
    const SceneExtract &sceneExtract = SceneExtract::getInstance();
    const Airg &airg = Airg::getInstance();
    const Navp &navp = Navp::getInstance();
    const Obj &obj = Obj::getInstance();
    const RecastAdapter recastAdapter = RecastAdapter::getInstance();
    const bool isSceneLoaded = scene.sceneLoaded;
    const bool isNavpLoaded = navp.navpLoaded;
    const bool isObjLoaded = obj.objLoaded;
    const bool isAreaSelected = isNavpLoaded && navp.selectedNavpAreaIndex != -1;

    setMenuItemEnabled(IDM_FILE_OPEN_AIRG, airg.canLoad());
    setMenuItemEnabled(IDM_FILE_OPEN_OBJ, obj.canLoad());

    setMenuItemEnabled(IDM_FILE_SAVE_SCENE, isSceneLoaded);
    setMenuItemEnabled(IDM_FILE_SAVE_NAVP, isNavpLoaded);
    setMenuItemEnabled(IDM_FILE_SAVE_AIRG, airg.canSave());
    setMenuItemEnabled(IDM_FILE_SAVE_OBJ, isObjLoaded);

    setMenuItemEnabled(IDM_EDIT_NAVP_STAIRS, isAreaSelected);

    setMenuItemEnabled(IDM_BUILD_NAVP, navp.canBuildNavp());
    setMenuItemEnabled(IDM_BUILD_AIRG, airg.canBuildAirg());
    setMenuItemEnabled(IDM_BUILD_OBJ_FROM_SCENE, obj.canBuildObjFromScene());
    setMenuItemEnabled(IDM_BUILD_OBJ_FROM_NAVP, obj.canBuildObjFromNavp());

    setMenuItemEnabled(IDM_EXTRACT_SCENE, sceneExtract.canExtractFromGame());
    setMenuItemEnabled(IDM_EXTRACT_SCENE_AND_BUILD_OBJ, sceneExtract.canExtractFromGameAndBuildObj());

    bool isStairs = false;
    if (isAreaSelected) {
        isStairs = navp.stairsAreaSelected();
    }
    setMenuItemChecked(IDM_EDIT_NAVP_STAIRS, isStairs, "Stairs Area");
}

void Menu::setMenuItemChecked(const UINT menuId, const bool isChecked, const char *itemName) {
    const HWND hwnd = Renderer::hwnd;
    if (!hwnd) return;
    const HMENU hMenu = GetMenu(hwnd);
    if (!hMenu) return;
    const UINT checkState = isChecked ? MF_CHECKED : MF_UNCHECKED;
    CheckMenuItem(hMenu, menuId, MF_BYCOMMAND | checkState);
    Logger::log(NK_DEBUG, ("Set " + std::string(itemName) + " to " + (isChecked ? "ON" : "OFF")).data());
}

int Menu::handleMenuClicked(const SDL_SysWMmsg *wmMsg) {
    Navp &navp = Navp::getInstance();
    Obj &obj = Obj::getInstance();
    Airg &airg = Airg::getInstance();
    Scene &scene = Scene::getInstance();
    SceneExtract &sceneExtract = SceneExtract::getInstance();
    if (wmMsg->subsystem == SDL_SYSWM_WINDOWS) {
        if (wmMsg->msg.win.msg == WM_COMMAND) {
            switch (LOWORD(wmMsg->msg.win.wParam)) {
                case IDM_FILE_OPEN_SCENE:
                    Scene::getInstance().handleOpenScenePressed();
                    Logger::log(NK_DEBUG, "File -> Open Scene clicked");
                    break;
                case IDM_FILE_OPEN_OBJ:
                    Obj::getInstance().handleOpenObjPressed();
                    Logger::log(NK_DEBUG, "File -> Open Obj clicked");
                    break;
                case IDM_FILE_OPEN_NAVP:
                    Navp::getInstance().handleOpenNavpPressed();
                    Logger::log(NK_DEBUG, "File -> Open Navp clicked");
                    break;
                case IDM_FILE_OPEN_AIRG:
                    Airg::getInstance().handleOpenAirgPressed();
                    Logger::log(NK_DEBUG, "File -> Open Airg clicked");
                    break;
                case IDM_FILE_SAVE_SCENE:
                    Scene::getInstance().handleSaveScenePressed();
                    Logger::log(NK_DEBUG, "File -> Save Scene clicked");
                    break;
                case IDM_FILE_SAVE_OBJ:
                    Obj::getInstance().handleSaveObjPressed();
                    Logger::log(NK_DEBUG, "File -> Save Obj clicked");
                    break;
                case IDM_FILE_SAVE_NAVP:
                    Navp::getInstance().handleSaveNavpPressed();
                    Logger::log(NK_DEBUG, "File -> Save Navp clicked");
                    break;
                case IDM_FILE_SAVE_AIRG:
                    Airg::getInstance().handleSaveAirgPressed();
                    Logger::log(NK_DEBUG, "File -> Save Airg clicked");
                    break;

                case IDM_FILE_EXIT:
                    return InputHandler::QUIT;

                case IDM_EDIT_NAVP_STAIRS:
                    navp.handleEditStairsClicked();
                    break;

                case IDM_VIEW_NAVP_SHOW_NAVP:
                    handleCheckboxMenuItem(IDM_VIEW_NAVP_SHOW_NAVP, Navp::getInstance().showNavp, "Show Navp");
                    break;
                case IDM_VIEW_NAVP_SHOW_INDICES:
                    handleCheckboxMenuItem(IDM_VIEW_NAVP_SHOW_INDICES, Navp::getInstance().showNavpIndices,
                                           "Show Navp Indices");
                    break;
                case IDM_VIEW_NAVP_SHOW_PF_EXCLUDE_BOXES:
                    handleCheckboxMenuItem(
                        IDM_VIEW_NAVP_SHOW_PF_EXCLUDE_BOXES, Navp::getInstance().showPfExclusionBoxes,
                        "Show Exclusion Boxes");
                    break;
                case IDM_VIEW_NAVP_SHOW_PF_SEED_POINTS:
                    handleCheckboxMenuItem(
                        IDM_VIEW_NAVP_SHOW_PF_SEED_POINTS, Navp::getInstance().showPfSeedPoints,
                        "Show Seed Points");
                    break;
                case IDM_VIEW_NAVP_SHOW_RECAST_DEBUG_INFO:
                    handleCheckboxMenuItem(
                        IDM_VIEW_NAVP_SHOW_RECAST_DEBUG_INFO, Navp::getInstance().showRecastDebugInfo,
                        "Show Recast Debug Info");
                    break;
                case IDM_VIEW_OBJ_SHOW_OBJ:
                    handleCheckboxMenuItem(
                        IDM_VIEW_OBJ_SHOW_OBJ, Obj::getInstance().showObj,
                        "Show Obj");
                    break;
                case IDM_VIEW_AIRG_SHOW_AIRG:
                    handleCheckboxMenuItem(
                        IDM_VIEW_AIRG_SHOW_AIRG, Airg::getInstance().showAirg,
                        "Show Airg");
                    break;
                case IDM_VIEW_AIRG_SHOW_INDICES:
                    handleCheckboxMenuItem(
                        IDM_VIEW_AIRG_SHOW_INDICES, Airg::getInstance().showAirgIndices,
                        "Show Airg Indices");
                    break;
                case IDM_VIEW_AIRG_SHOW_GRID:
                    handleCheckboxMenuItem(
                        IDM_VIEW_AIRG_SHOW_GRID, Grid::getInstance().showGrid,
                        "Show Grid");
                    break;
                case IDM_VIEW_AIRG_SHOW_RECAST_DEBUG_INFO:
                    handleCheckboxMenuItem(
                        IDM_VIEW_AIRG_SHOW_RECAST_DEBUG_INFO, Airg::getInstance().showRecastDebugInfo,
                        "Show Recast Debug Info");
                    break;
                case IDM_VIEW_LOG_SHOW_LOG:
                    handleCheckboxMenuItem(
                        IDM_VIEW_LOG_SHOW_LOG, Gui::getInstance().showLog,
                        "Show Log");
                    break;
                case IDM_VIEW_AIRG_CELL_COLOR_OFF:
                case IDM_VIEW_AIRG_CELL_COLOR_BITMAP:
                case IDM_VIEW_AIRG_CELL_COLOR_VISION_DATA:
                case IDM_VIEW_AIRG_CELL_COLOR_LAYER:
                    handleCellColorDataRadioMenuItem(LOWORD(wmMsg->msg.win.wParam));
                    break;

                case IDM_BUILD_OBJ_FROM_NAVP:
                    obj.handleBuildObjFromNavpClicked();
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
                    // sceneExtract.backgroundWorker.emplace(&Navp::buildNavp, &navp);
                    break;

                case IDM_HELP_ABOUT: {
                    const std::string currentVersionStr =
                            std::string(NavKit_VERSION_MAJOR) + "." +
                            std::string(NavKit_VERSION_MINOR) + "." +
                            std::string(NavKit_VERSION_PATCH);
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "About",
                                             ("NavKit version " + currentVersionStr).data(), nullptr);
                    break;
                }

                case IDM_SETTINGS_NAVKIT:
                    Settings::getInstance().showSettingsDialog();
                    Logger::log(NK_DEBUG, "Settings -> NavKit Settings clicked");
                    break;

                default:
                    break;
            }
        }
    }
    return 0;
}
