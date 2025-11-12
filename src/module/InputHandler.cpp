#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/RecastDemo/imgui.h"

#include <algorithm>
#include <SDL.h>
#include <SDL_opengl.h>

#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/Scene.h"

const int InputHandler::QUIT = 1;

InputHandler::InputHandler() {
    mouseButtonMask = 0;
    mousePos[0] = 0, mousePos[1] = 0;
    origMousePos[0] = 0, origMousePos[1] = 0;
    mouseScroll = 0;
    moveFront = 0.0f, moveBack = 0.0f, moveLeft = 0.0f, moveRight = 0.0f, moveUp = 0.0f, moveDown = 0.0f;
    scrollZoom = 0;
    movedDuringRotate = false;
    rotate = false;
}

int InputHandler::handleInput() {
    SDL_Event event;
    mouseScroll = 0;
    const Gui &gui = Gui::getInstance();
    Renderer &renderer = Renderer::getInstance();
    Airg &airg = Airg::getInstance();
    Navp &navp = Navp::getInstance();
    Obj &obj = Obj::getInstance();
    bool done = false;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    renderer.handleResize();
                }
                if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                    renderer.updateFrameRate();
                }
                break;

            case SDL_KEYDOWN:
                // Handle any key presses here.
                if (event.key.keysym.sym == SDLK_TAB) {
                    Gui::getInstance().showMenu = !gui.showMenu;
                }
                break;

            case SDL_MOUSEWHEEL:
                if (event.wheel.y < 0) {
                    // wheel down
                    if (gui.mouseOverMenu) {
                        mouseScroll++;
                    } else {
                        scrollZoom += 1.0f;
                    }
                } else {
                    if (gui.mouseOverMenu) {
                        mouseScroll--;
                    } else {
                        scrollZoom -= 1.0f;
                    }
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    if (!gui.mouseOverMenu) {
                        // Rotate view
                        rotate = true;
                        movedDuringRotate = false;
                        origMousePos[0] = mousePos[0];
                        origMousePos[1] = mousePos[1];
                        renderer.origCameraEulers[0] = renderer.cameraEulers[0];
                        renderer.origCameraEulers[1] = renderer.cameraEulers[1];
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                // Handle mouse clicks here.
                if (event.button.button == SDL_BUTTON_RIGHT) {
                    rotate = false;
                } else if (event.button.button == SDL_BUTTON_LEFT) {
                    if (!gui.mouseOverMenu) {
                        navp.doNavpHitTest = true;
                        navp.doNavpExclusionBoxHitTest = true;
                        navp.doNavpPfSeedPointHitTest = true;
                        airg.doAirgHitTest = true;
                        obj.doObjHitTest = true;
                    }
                }

                break;

            case SDL_MOUSEMOTION:
                mousePos[0] = event.motion.x;
                mousePos[1] = renderer.height - 1 - event.motion.y;

                if (rotate) {
                    const auto dx = static_cast<float>(mousePos[0] - origMousePos[0]);
                    const auto dy = static_cast<float>(mousePos[1] - origMousePos[1]);
                    renderer.cameraEulers[0] = renderer.origCameraEulers[0] - dy * CAMERA_ROTATION_SENSITIVITY;
                    renderer.cameraEulers[1] = renderer.origCameraEulers[1] + dx * CAMERA_ROTATION_SENSITIVITY;
                    if (dx * dx + dy * dy > 3 * 3) {
                        movedDuringRotate = true;
                    }
                }
                break;

            case SDL_QUIT:
                done = true;
                break;
            case SDL_SYSWMEVENT:
                if (handleMenu(event.syswm.msg) == QUIT) {
                    done = true;
                }
                break;
            default:
                break;
        }
    }
    mouseButtonMask = 0;
    const Uint32 mouseState = SDL_GetMouseState(nullptr, nullptr);
    if (mouseState & SDL_BUTTON_LMASK)
        mouseButtonMask |= IMGUI_MBUT_LEFT;
    if (mouseState & SDL_BUTTON_RMASK)
        mouseButtonMask |= IMGUI_MBUT_RIGHT;

    keybSpeed = BASE_KEYBOARD_SPEED;
    if (SDL_GetModState() & KMOD_SHIFT) {
        keybSpeed *= SHIFT_SPEED_MULTIPLIER;
    }
    if (SDL_GetModState() & KMOD_CTRL) {
        keybSpeed /= CTRL_SPEED_DIVISOR;
    }
    if (done) {
        return QUIT;
    }
    return 0;
}

void InputHandler::handleMovement(const float dt, const double *modelviewMatrix) {
    // Handle keyboard movement.
    const Uint8 *keyState = SDL_GetKeyboardState(nullptr);

    // A helper lambda makes the movement logic reusable and easier to read.
    auto calculateMovement = [&](float currentValue, bool isPressed) {
        constexpr float accelerationFactor = 4.0f; // Give the magic number a name
        const float direction = isPressed ? 1.0f : -1.0f;
        return std::clamp(currentValue + dt * accelerationFactor * direction, 0.0f, 1.0f);
    };

    moveFront = calculateMovement(moveFront, keyState[SDL_SCANCODE_W] || keyState[SDL_SCANCODE_UP]);
    moveBack = calculateMovement(moveBack, keyState[SDL_SCANCODE_S] || keyState[SDL_SCANCODE_DOWN]);
    moveLeft = calculateMovement(moveLeft, keyState[SDL_SCANCODE_A] || keyState[SDL_SCANCODE_LEFT]);
    moveRight = calculateMovement(moveRight, keyState[SDL_SCANCODE_D] || keyState[SDL_SCANCODE_RIGHT]);
    moveUp = calculateMovement(moveUp, keyState[SDL_SCANCODE_Q] || keyState[SDL_SCANCODE_PAGEUP]);
    moveDown = calculateMovement(moveDown, keyState[SDL_SCANCODE_E] || keyState[SDL_SCANCODE_PAGEDOWN]);

    const float moveX = (moveRight - moveLeft) * keybSpeed * dt;
    const float moveY = (moveBack - moveFront) * keybSpeed * dt + scrollZoom * 2.0f;
    scrollZoom = 0;

    Renderer &renderer = Renderer::getInstance();
    renderer.cameraPos[0] += moveX * static_cast<float>(modelviewMatrix[0]);
    renderer.cameraPos[1] += moveX * static_cast<float>(modelviewMatrix[4]);
    renderer.cameraPos[2] += moveX * static_cast<float>(modelviewMatrix[8]);

    renderer.cameraPos[0] += moveY * static_cast<float>(modelviewMatrix[2]);
    renderer.cameraPos[1] += moveY * static_cast<float>(modelviewMatrix[6]);
    renderer.cameraPos[2] += moveY * static_cast<float>(modelviewMatrix[10]);

    renderer.cameraPos[1] += (moveUp - moveDown) * keybSpeed * dt;
}

void InputHandler::hitTest() const {
    Gui &gui = Gui::getInstance();
    Navp &navp = Navp::getInstance();
    Airg &airg = Airg::getInstance();
    Obj &obj = Obj::getInstance();
    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    Renderer &renderer = Renderer::getInstance();
    if (!gui.mouseOverMenu) {
        if ((navp.doNavpHitTest && navp.navpLoaded && navp.showNavp) ||
            (navp.doNavpExclusionBoxHitTest && navp.showPfExclusionBoxes) ||
            (navp.doNavpPfSeedPointHitTest && navp.showPfSeedPoints) ||
            (airg.doAirgHitTest && airg.airgLoaded && airg.showAirg) ||
            (obj.doObjHitTest && obj.objLoaded && obj.showObj)) {
            HitTestResult hitTestResult = renderer.hitTestRender(mousePos[0], mousePos[1]);
            if (hitTestResult.type == NAVMESH_AREA) {
                if (hitTestResult.selectedIndex == navp.selectedNavpAreaIndex) {
                    navp.setSelectedNavpAreaIndex(-1);
                } else {
                    navp.setSelectedNavpAreaIndex(hitTestResult.selectedIndex);
                }
            } else if (hitTestResult.type == AIRG_WAYPOINT) {
                if (hitTestResult.selectedIndex == airg.selectedWaypointIndex) {
                    airg.setSelectedAirgWaypointIndex(-1);
                    airg.connectWaypointModeEnabled = false;
                } else {
                    if (airg.connectWaypointModeEnabled) {
                        airg.connectWaypoints(airg.selectedWaypointIndex, hitTestResult.selectedIndex);
                    } else {
                        airg.setSelectedAirgWaypointIndex(hitTestResult.selectedIndex);
                    }
                    airg.connectWaypointModeEnabled = false;
                }
            } else if (hitTestResult.type == PF_SEED_POINT) {
                if (hitTestResult.selectedIndex == navp.selectedPfSeedPointIndex) {
                    navp.setSelectedPfSeedPointIndex(-1);
                } else {
                    navp.setSelectedPfSeedPointIndex(hitTestResult.selectedIndex);
                }
            } else if (hitTestResult.type == PF_EXCLUSION_BOX) {
                if (hitTestResult.selectedIndex == navp.selectedExclusionBoxIndex) {
                    navp.setSelectedExclusionBoxIndex(-1);
                } else {
                    navp.setSelectedExclusionBoxIndex(hitTestResult.selectedIndex);
                }
            } else {
                navp.setSelectedNavpAreaIndex(-1);
                airg.setSelectedAirgWaypointIndex(-1);
                if (obj.showObj && obj.objLoaded) {
                    recastAdapter.doHitTest(mousePos[0], mousePos[1]);
                }
            }
            navp.doNavpHitTest = false;
            navp.doNavpExclusionBoxHitTest = false;
            navp.doNavpPfSeedPointHitTest = false;
            airg.doAirgHitTest = false;
            obj.doObjHitTest = false;
        }
    }
}

void InputHandler::setMenuItemEnabled(UINT menuId, bool isEnabled) {
    HWND hwnd = Renderer::hwnd;
    if (!hwnd) return;
    HMENU hMenu = GetMenu(hwnd);
    if (!hMenu) return;
    UINT flags = MF_BYCOMMAND | (isEnabled ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(hMenu, menuId, flags);
}

void InputHandler::handleCheckboxMenuItem(const UINT menuId, bool &stateVariable, const char *itemName) {
    HWND hwnd = Renderer::hwnd;
    HMENU hMenu = GetMenu(hwnd);
    stateVariable = !stateVariable;
    const UINT checkState = stateVariable ? MF_CHECKED : MF_UNCHECKED;
    CheckMenuItem(hMenu, menuId, MF_BYCOMMAND | checkState);
    Logger::log(NK_INFO, ("Toggled " + std::string(itemName) + " " + (stateVariable ? "ON" : "OFF")).data());
}

void InputHandler::handleCellColorDataRadioMenuItem(const int selectedMenuId) {
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
    Logger::log(NK_INFO, ("Set Cell Color Data Source to " + std::string(itemName)).data());
}


int InputHandler::handleMenu(const SDL_SysWMmsg *wmMsg) {
    if (wmMsg->subsystem == SDL_SYSWM_WINDOWS) {
        if (wmMsg->msg.win.msg == WM_COMMAND) {
            switch (LOWORD(wmMsg->msg.win.wParam)) {
                case IDM_FILE_OPEN_SCENE:
                    Scene::getInstance().handleOpenScenePressed();
                    Logger::log(NK_INFO, "File -> Open Scene clicked");
                    break;
                case IDM_FILE_OPEN_OBJ:
                    Obj::getInstance().handleOpenObjPressed();
                    Logger::log(NK_INFO, "File -> Open Obj clicked");
                    break;
                case IDM_FILE_OPEN_NAVP:
                    Navp::getInstance().handleOpenNavpPressed();
                    Logger::log(NK_INFO, "File -> Open Navp clicked");
                    break;
                case IDM_FILE_OPEN_AIRG:
                    Airg::getInstance().handleOpenAirgPressed();
                    Logger::log(NK_INFO, "File -> Open Airg clicked");
                    break;
                case IDM_FILE_SAVE_SCENE:
                    Scene::getInstance().handleSaveScenePressed();
                    Logger::log(NK_INFO, "File -> Save Scene clicked");
                    break;
                case IDM_FILE_SAVE_OBJ:
                    Obj::getInstance().handleSaveObjPressed();
                    Logger::log(NK_INFO, "File -> Save Obj clicked");
                    break;
                case IDM_FILE_SAVE_NAVP:
                    Navp::getInstance().handleSaveNavpPressed();
                    Logger::log(NK_INFO, "File -> Save Navp clicked");
                    break;
                case IDM_FILE_SAVE_AIRG:
                    Airg::getInstance().handleSaveAirgPressed();
                    Logger::log(NK_INFO, "File -> Save Airg clicked");
                    break;

                case IDM_FILE_EXIT:
                    return QUIT;

                case IDM_HELP_ABOUT: {
                    const std::string currentVersionStr =
                            std::string(NavKit_VERSION_MAJOR) + "." +
                            std::string(NavKit_VERSION_MINOR) + "." +
                            std::string(NavKit_VERSION_PATCH);
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "About",
                                             ("NavKit version " + currentVersionStr).data(), nullptr);
                }
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

                default:
                    break;
            }
        }
    }
    return 0;
}
