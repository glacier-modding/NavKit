#pragma once

class InputHandler {
    explicit InputHandler();
public:
    static InputHandler& getInstance() {
        static InputHandler instance;
        return instance;
    }
    int handleInput();

    void hitTest();

    void handleMovement(float dt, double* modelviewMatrix);

    int mousePos[2]{};
    int origMousePos[2]{};
    int mouseScroll;
    bool resized;
    bool moved;

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
};
