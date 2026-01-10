#pragma once
#include <SDL_syswm.h>

class InputHandler {
    explicit InputHandler();

public:
    static InputHandler &getInstance() {
        static InputHandler instance;
        return instance;
    }

    int handleInput();

    void hitTest() const;


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

    static constexpr int QUIT = 1;
    static constexpr float BASE_KEYBOARD_SPEED = 22.0f;
    static constexpr float SHIFT_SPEED_MULTIPLIER = 4.0f;
    static constexpr float CTRL_SPEED_DIVISOR = 4.0f;
    static constexpr float CAMERA_ROTATION_SENSITIVITY = 0.25f;
};
