#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/RecastDemo/imgui.h"

#include <algorithm>
#include <SDL.h>
#include <SDL_opengl.h>

#include "../../include/NavKit/module/NavKitSettings.h"


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
                if (SDL_SysWMmsg* wmMsg = event.syswm.msg;
                    (NavKitSettings::hSettingsDialog && IsDialogMessage(NavKitSettings::hSettingsDialog, reinterpret_cast<LPMSG>(&wmMsg->msg.win.msg))) ||
                    (Airg::hAirgDialog && IsDialogMessage(Airg::hAirgDialog, reinterpret_cast<LPMSG>(&wmMsg->msg.win.msg))) ||
                    (Scene::hSceneDialog && IsDialogMessage(Scene::hSceneDialog, reinterpret_cast<LPMSG>(&wmMsg->msg.win.msg))) ||
                    (RecastAdapter::hRecastDialog && IsDialogMessage(RecastAdapter::hRecastDialog, reinterpret_cast<LPMSG>(&wmMsg->msg.win.msg)))) {
                   continue;
                }
                if (Menu::handleMenuClicked(event.syswm.msg) == QUIT) {
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
                } else {
                    if (airg.connectWaypointModeEnabled) {
                        airg.connectWaypoints(airg.selectedWaypointIndex, hitTestResult.selectedIndex);
                    } else {
                        airg.setSelectedAirgWaypointIndex(hitTestResult.selectedIndex);
                    }
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
    Menu::updateMenuState();
}
