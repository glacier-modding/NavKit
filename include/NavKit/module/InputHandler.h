#pragma once
#include <SDL_events.h>
#include <SDL_syswm.h>

#include "Airg.h"

class InputHandler {
    explicit InputHandler();

public:
    static InputHandler &getInstance() {
        static InputHandler instance;
        return instance;
    }

    static int handleMenu(const SDL_SysWMmsg * wmMsg);

    int handleInput();

    void hitTest() const;

    static void handleCheckboxMenuItem(UINT menuId, bool &stateVariable, const char *itemName);

    static void handleCellColorDataRadioMenuItem(int selectedMenuId);

    void handleMovement(float dt, const double *modelviewMatrix);

    int mousePos[2]{};
    int origMousePos[2]{};
    int mouseScroll;

    float scrollZoom;
    bool rotate;
    bool movedDuringRotate;
    float keybSpeed = 22.0f;

    unsigned char mouseButtonMask;

    float moveFront;
    float moveBack;
    float moveLeft;
    float moveRight;
    float moveUp;
    float moveDown;

    static const int QUIT;
    static constexpr float BASE_KEYBOARD_SPEED = 22.0f;
    static constexpr float SHIFT_SPEED_MULTIPLIER = 4.0f;
    static constexpr float CTRL_SPEED_DIVISOR = 4.0f;
    static constexpr float CAMERA_ROTATION_SENSITIVITY = 0.25f;
};
