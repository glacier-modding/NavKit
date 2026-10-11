#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Gui.h"
#include "../../include/NavKit/module/Menu.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/RecastDemo/imgui.h"

#include <algorithm>
#include <iterator>
#include <SDL.h>
#include <wx/utils.h>

namespace {
    enum MovementAction { MOVE_FRONT, MOVE_BACK, MOVE_LEFT, MOVE_RIGHT, MOVE_UP, MOVE_DOWN };

    struct MovementKey {
        int keyCode;
        MovementAction action;
    };

    // Every key that moves the camera. InputHandler::pressedMovementKeys holds one bit per entry, in table order.
    constexpr MovementKey MOVEMENT_KEYS[] = {
        {'W', MOVE_FRONT},
        {WXK_UP, MOVE_FRONT},
        {'S', MOVE_BACK},
        {WXK_DOWN, MOVE_BACK},
        {'A', MOVE_LEFT},
        {WXK_LEFT, MOVE_LEFT},
        {'D', MOVE_RIGHT},
        {WXK_RIGHT, MOVE_RIGHT},
        {'Q', MOVE_UP},
        {WXK_PAGEUP, MOVE_UP},
        {'E', MOVE_DOWN},
        {WXK_PAGEDOWN, MOVE_DOWN},
    };
    static_assert(std::size(MOVEMENT_KEYS) <= 32, "pressedMovementKeys needs one bit per movement key");

    int movementKeyIndex(const int keyCode) {
        for (int i = 0; i < static_cast<int>(std::size(MOVEMENT_KEYS)); ++i) {
            if (MOVEMENT_KEYS[i].keyCode == keyCode) {
                return i;
            }
        }
        return -1;
    }
} // namespace

InputHandler::InputHandler() {
    mouseButtonMask = 0;
    mousePos[0] = 0, mousePos[1] = 0;
    origMousePos[0] = 0, origMousePos[1] = 0;
    mouseScroll = 0;
    moveFront = 0.0f, moveBack = 0.0f, moveLeft = 0.0f, moveRight = 0.0f, moveUp = 0.0f, moveDown = 0.0f;
    scrollZoom = 0;
    movedDuringRotate = false;
    rotate = false;
    pendingMouseScroll = 0;
    wheelNotches = 0.0f;
    modifierKeyDown = false;
    pressedMovementKeys = 0;
}

int InputHandler::handleInput() {
    // Mouse and keyboard events go to the wxWidgets render canvas (see MainFrame), not to SDL: wxWidgets owns the
    // native view, and on macOS it consumes the events before SDL's responder ever sees them. SDL only reports
    // window and quit events here.
    SDL_Event event;
    Renderer& renderer = Renderer::getInstance();
    bool done = false;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                renderer.handleResize();

                Renderer::FullscreenMode mode = Renderer::WINDOWED;
                if (getMainFrame()->IsFullScreen()) {
                    mode = Renderer::BORDERLESS_FULLSCREEN;
                } else if (getMainFrame()->IsMaximized()) {
                    mode = Renderer::MAXIMIZED;
                }
                renderer.handleFullscreen(mode);
            }
            if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                renderer.handleMoved();
            }
            break;

        case SDL_QUIT:
            done = true;
            break;
        default:
            break;
        }
    }

    mouseScroll = pendingMouseScroll;
    pendingMouseScroll = 0;

    const wxMouseState mouseState = wxGetMouseState();
    mouseButtonMask = 0;
    if (mouseState.LeftIsDown()) {
        mouseButtonMask |= IMGUI_MBUT_LEFT;
    }
    if (mouseState.RightIsDown()) {
        mouseButtonMask |= IMGUI_MBUT_RIGHT;
    }
    modifierKeyDown = mouseState.ShiftDown() || mouseState.ControlDown() || mouseState.RawControlDown() ||
        mouseState.AltDown() || mouseState.MetaDown();

    keybSpeed = BASE_KEYBOARD_SPEED;
    if (mouseState.ShiftDown()) {
        keybSpeed *= SHIFT_SPEED_MULTIPLIER;
    }
    // RawControlDown is the physical Ctrl key on every platform (ControlDown means Cmd on macOS).
    if (mouseState.RawControlDown()) {
        keybSpeed /= CTRL_SPEED_DIVISOR;
    }
    if (done) {
        return QUIT;
    }
    return 0;
}

void InputHandler::handleMouseMove(const int x, const int y) {
    Renderer& renderer = Renderer::getInstance();
    mousePos[0] = x;
    mousePos[1] = renderer.height - y;

    if (rotate) {
        const auto dx = static_cast<float>(mousePos[0] - origMousePos[0]);
        const auto dy = static_cast<float>(mousePos[1] - origMousePos[1]);
        renderer.cameraEulers[0] = renderer.origCameraEulers[0] - dy * CAMERA_ROTATION_SENSITIVITY;
        renderer.cameraEulers[1] = renderer.origCameraEulers[1] + dx * CAMERA_ROTATION_SENSITIVITY;
        if (dx * dx + dy * dy > 3 * 3) {
            movedDuringRotate = true;
        }
    }
}

void InputHandler::handleMouseButtonDown(const MouseButton button) {
    if (button != RIGHT_MOUSE_BUTTON) {
        return;
    }
    if (const Gui& gui = Gui::getInstance(); gui.mouseOverMenu) {
        return;
    }
    // Rotate view
    Renderer& renderer = Renderer::getInstance();
    rotate = true;
    movedDuringRotate = false;
    origMousePos[0] = mousePos[0];
    origMousePos[1] = mousePos[1];
    renderer.origCameraEulers[0] = renderer.cameraEulers[0];
    renderer.origCameraEulers[1] = renderer.cameraEulers[1];
}

void InputHandler::handleMouseButtonUp(const MouseButton button) {
    if (button == RIGHT_MOUSE_BUTTON) {
        rotate = false;
        return;
    }
    if (const Gui& gui = Gui::getInstance(); gui.mouseOverMenu) {
        return;
    }
    Navp& navp = Navp::getInstance();
    Airg& airg = Airg::getInstance();
    SceneMesh& obj = SceneMesh::getInstance();
    navp.doNavpHitTest = true;
    navp.doNavpExclusionBoxHitTest = true;
    navp.doNavpPfSeedPointHitTest = true;
    airg.doAirgHitTest = true;
    obj.doGlbHitTest = true;
}

void InputHandler::handleMouseWheel(const int rotation, const int delta) {
    if (delta <= 0) {
        return;
    }
    wheelNotches += static_cast<float>(rotation) / static_cast<float>(delta);
    const int notches = static_cast<int>(wheelNotches);
    if (notches == 0) {
        return;
    }
    wheelNotches -= static_cast<float>(notches);
    // Wheel down (negative rotation) scrolls the menu down and moves the camera back.
    if (Gui::getInstance().mouseOverMenu) {
        pendingMouseScroll -= notches;
    } else {
        scrollZoom -= static_cast<float>(notches);
    }
}

bool InputHandler::handleKeyDown(const int keyCode, const bool altDown) {
    // Movement keys are tracked from the canvas's own key events rather than polled: polling the keyboard state
    // needs the Input Monitoring permission on macOS.
    if (const int index = movementKeyIndex(keyCode); index != -1) {
        pressedMovementKeys |= 1u << index;
        return true;
    }
    switch (keyCode) {
    case WXK_TAB: {
        Gui& gui = Gui::getInstance();
        gui.showMenu = !gui.showMenu;
        return true;
    }
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
        if (!altDown) {
            return false;
        }
        Renderer::getInstance().handleFullscreen(
            getMainFrame()->IsFullScreen() ? Renderer::WINDOWED : Renderer::BORDERLESS_FULLSCREEN);
        return true;
    default:
        return false;
    }
}

bool InputHandler::handleKeyUp(const int keyCode) {
    const int index = movementKeyIndex(keyCode);
    if (index == -1) {
        return false;
    }
    pressedMovementKeys &= ~(1u << index);
    return true;
}

void InputHandler::handleKeyboardFocusLost() {
    pressedMovementKeys = 0;
}

void InputHandler::handleMovement(const float dt, const double* modelviewMatrix) {
    // Handle keyboard movement.
    auto isPressed = [this](const MovementAction action) {
        for (size_t i = 0; i < std::size(MOVEMENT_KEYS); ++i) {
            if (MOVEMENT_KEYS[i].action == action && (pressedMovementKeys & (1u << i)) != 0) {
                return true;
            }
        }
        return false;
    };

    // A helper lambda makes the movement logic reusable and easier to read.
    auto calculateMovement = [&](const float currentValue, const bool pressed) {
        constexpr float accelerationFactor = 4.0f; // Give the magic number a name
        const float direction = pressed ? 1.0f : -1.0f;
        return std::clamp(currentValue + dt * accelerationFactor * direction, 0.0f, 1.0f);
    };

    moveFront = calculateMovement(moveFront, isPressed(MOVE_FRONT));
    moveBack = calculateMovement(moveBack, isPressed(MOVE_BACK));
    moveLeft = calculateMovement(moveLeft, isPressed(MOVE_LEFT));
    moveRight = calculateMovement(moveRight, isPressed(MOVE_RIGHT));
    moveUp = calculateMovement(moveUp, isPressed(MOVE_UP));
    moveDown = calculateMovement(moveDown, isPressed(MOVE_DOWN));

    const float moveX = (moveRight - moveLeft) * keybSpeed * dt;
    const float moveY = (moveBack - moveFront) * keybSpeed * dt + scrollZoom * 2.0f;
    scrollZoom = 0;

    Renderer& renderer = Renderer::getInstance();
    renderer.cameraPos[0] += moveX * static_cast<float>(modelviewMatrix[0]);
    renderer.cameraPos[1] += moveX * static_cast<float>(modelviewMatrix[4]);
    renderer.cameraPos[2] += moveX * static_cast<float>(modelviewMatrix[8]);

    renderer.cameraPos[0] += moveY * static_cast<float>(modelviewMatrix[2]);
    renderer.cameraPos[1] += moveY * static_cast<float>(modelviewMatrix[6]);
    renderer.cameraPos[2] += moveY * static_cast<float>(modelviewMatrix[10]);

    renderer.cameraPos[1] += (moveUp - moveDown) * keybSpeed * dt;
}

void InputHandler::hitTest() const {
    const Gui& gui = Gui::getInstance();
    Navp& navp = Navp::getInstance();
    Airg& airg = Airg::getInstance();
    SceneMesh& sceneMesh = SceneMesh::getInstance();
    RecastAdapter& recastAdapter = RecastAdapter::getInstance();
    const Renderer& renderer = Renderer::getInstance();
    if (!gui.mouseOverMenu) {
        if ((navp.doNavpHitTest && navp.navpLoaded && navp.showNavp) ||
            (navp.doNavpExclusionBoxHitTest && navp.showPfExclusionBoxes) ||
            (navp.doNavpPfSeedPointHitTest && navp.showPfSeedPoints) ||
            (airg.doAirgHitTest && airg.airgLoaded && airg.showAirg) ||
            (sceneMesh.doGlbHitTest && sceneMesh.glbLoaded && sceneMesh.showGlb)) {
            SceneMeshHitTestResult sceneMeshHitResult;
            if (sceneMesh.showGlb && sceneMesh.glbLoaded && sceneMesh.doGlbHitTest) {
                sceneMeshHitResult = recastAdapter.doHitTest(mousePos[0], mousePos[1]);
            }

            if (const HitTestResult hitTestResult = renderer.hitTestRender(mousePos[0], mousePos[1]);
                hitTestResult.type == AIRG_WAYPOINT) {
                if (hitTestResult.selectedIndex == airg.selectedWaypointIndex) {
                    airg.setSelectedAirgWaypointIndex(-1);
                } else {
                    if (airg.connectWaypointModeEnabled) {
                        airg.connectWaypoints(airg.selectedWaypointIndex, hitTestResult.selectedIndex);
                    } else if (airg.disconnectWaypointModeEnabled) {
                        airg.disconnectWaypoints(airg.selectedWaypointIndex, hitTestResult.selectedIndex);
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
            } else if (hitTestResult.type == NAVMESH_AREA) {
                if (hitTestResult.selectedIndex == navp.selectedNavpAreaIndex) {
                    navp.setSelectedNavpAreaIndex(-1);
                } else {
                    navp.setSelectedNavpAreaIndex(hitTestResult.selectedIndex);
                }
            } else {
                if (hitTestResult.type == NONE && sceneMeshHitResult.hitIndex == -1) {
                    navp.setSelectedNavpAreaIndex(-1);
                    airg.setSelectedAirgWaypointIndex(-1);
                    navp.setSelectedExclusionBoxIndex(-1);
                    navp.setSelectedPfSeedPointIndex(-1);
                }
                if (sceneMeshHitResult.hitIndex != -1) {
                    recastAdapter.setMarker(sceneMeshHitResult);
                }
            }

            navp.doNavpHitTest = false;
            navp.doNavpExclusionBoxHitTest = false;
            navp.doNavpPfSeedPointHitTest = false;
            airg.doAirgHitTest = false;
            sceneMesh.doGlbHitTest = false;
        }
    }
    Menu::updateMenuState();
}
