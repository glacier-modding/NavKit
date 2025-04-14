#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/RecastDemo/imgui.h"

#include <algorithm>
#include <SDL.h>
#include <SDL_opengl.h>

const int InputHandler::QUIT = 1;

InputHandler::InputHandler() {
    mouseButtonMask = 0;
    mousePos[0] = 0, mousePos[1] = 0;
    origMousePos[0] = 0, origMousePos[1] = 0;
    mouseScroll = 0;
    resized = false;
    moved = false;
    moveFront = 0.0f, moveBack = 0.0f, moveLeft = 0.0f, moveRight = 0.0f, moveUp = 0.0f, moveDown = 0.0f;
    scrollZoom = 0;
    movedDuringRotate = false;
    rotate = false;
}

int InputHandler::handleInput() {
    // Handle input events.
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
                    resized = true;
                }
                if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                    moved = true;
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
                    int dx = mousePos[0] - origMousePos[0];
                    int dy = mousePos[1] - origMousePos[1];
                    renderer.cameraEulers[0] = renderer.origCameraEulers[0] - dy * 0.25f;
                    renderer.cameraEulers[1] = renderer.origCameraEulers[1] + dx * 0.25f;
                    if (dx * dx + dy * dy > 3 * 3) {
                        movedDuringRotate = true;
                    }
                }
                break;

            case SDL_QUIT:
                done = true;
                break;
            default:
                break;
        }
    }
    mouseButtonMask = 0;
    if (SDL_GetMouseState(0, 0) & SDL_BUTTON_LMASK)
        mouseButtonMask |= IMGUI_MBUT_LEFT;
    if (SDL_GetMouseState(0, 0) & SDL_BUTTON_RMASK)
        mouseButtonMask |= IMGUI_MBUT_RIGHT;

    keybSpeed = 22.0f;
    if (SDL_GetModState() & KMOD_SHIFT) {
        keybSpeed *= 4.0f;
    }
    if (SDL_GetModState() & KMOD_CTRL) {
        keybSpeed /= 4.0f;
    }
    if (done) {
        return QUIT;
    }
    return 0;
}

void InputHandler::handleMovement(float dt, double *modelviewMatrix) {
    // Handle keyboard movement.
    const Uint8 *keyState = SDL_GetKeyboardState(nullptr);
    moveFront = std::clamp(
        moveFront + dt * 4 * static_cast<float>(keyState[SDL_SCANCODE_W] || keyState[SDL_SCANCODE_UP] ? 1 : -1), 0.0f,
        1.0f);
    moveLeft = std::clamp(
        moveLeft + dt * 4 * static_cast<float>(keyState[SDL_SCANCODE_A] || keyState[SDL_SCANCODE_LEFT] ? 1 : -1), 0.0f,
        1.0f);
    moveBack = std::clamp(
        moveBack + dt * 4 * static_cast<float>(keyState[SDL_SCANCODE_S] || keyState[SDL_SCANCODE_DOWN] ? 1 : -1), 0.0f,
        1.0f);
    moveRight = std::clamp(
        moveRight + dt * 4 * static_cast<float>(keyState[SDL_SCANCODE_D] || keyState[SDL_SCANCODE_RIGHT] ? 1 : -1),
        0.0f, 1.0f);
    moveUp = std::clamp(
        moveUp + dt * 4 * static_cast<float>(keyState[SDL_SCANCODE_Q] || keyState[SDL_SCANCODE_PAGEUP] ? 1 : -1), 0.0f,
        1.0f);
    moveDown = std::clamp(
        moveDown + dt * 4 * static_cast<float>(keyState[SDL_SCANCODE_E] || keyState[SDL_SCANCODE_PAGEDOWN] ? 1 : -1),
        0.0f, 1.0f);
    float movex = (moveRight - moveLeft) * keybSpeed * dt;
    float movey = (moveBack - moveFront) * keybSpeed * dt + scrollZoom * 2.0f;
    scrollZoom = 0;

    Renderer &renderer = Renderer::getInstance();
    renderer.cameraPos[0] += movex * static_cast<float>(modelviewMatrix[0]);
    renderer.cameraPos[1] += movex * static_cast<float>(modelviewMatrix[4]);
    renderer.cameraPos[2] += movex * static_cast<float>(modelviewMatrix[8]);

    renderer.cameraPos[0] += movey * static_cast<float>(modelviewMatrix[2]);
    renderer.cameraPos[1] += movey * static_cast<float>(modelviewMatrix[6]);
    renderer.cameraPos[2] += movey * static_cast<float>(modelviewMatrix[10]);

    renderer.cameraPos[1] += (moveUp - moveDown) * keybSpeed * dt;
}


void InputHandler::hitTest() {
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
            if (hitTestResult.type == HitTestType::NAVMESH_AREA) {
                if (hitTestResult.selectedIndex == navp.selectedNavpAreaIndex) {
                    navp.setSelectedNavpAreaIndex(-1);
                } else {
                    navp.setSelectedNavpAreaIndex(hitTestResult.selectedIndex);
                }
            } else if (hitTestResult.type == HitTestType::AIRG_WAYPOINT) {
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
            } else if (hitTestResult.type == HitTestType::PF_SEED_POINT) {
                if (hitTestResult.selectedIndex == navp.selectedPfSeedPointIndex) {
                    navp.setSelectedPfSeedPointIndex(-1);
                } else {
                    navp.setSelectedPfSeedPointIndex(hitTestResult.selectedIndex);
                }
            } else if (hitTestResult.type == HitTestType::PF_EXCLUSION_BOX) {
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
