#pragma once

class InputHandler {
    explicit InputHandler();

public:
    enum MouseButton { LEFT_MOUSE_BUTTON, RIGHT_MOUSE_BUTTON };

    static InputHandler& getInstance() {
        static InputHandler instance;
        return instance;
    }

    /// Polls SDL for window and quit events and samples the mouse button and modifier key state for this frame.
    /// Mouse and keyboard input itself arrives through the wxWidgets event handlers of the render canvas.
    int handleInput();

    void hitTest() const;

    void handleMovement(float dt, const double* modelviewMatrix);

    /// Called by the render canvas event handlers. Positions are in canvas client coordinates.
    void handleMouseMove(int x, int y);

    void handleMouseButtonDown(MouseButton button);

    void handleMouseButtonUp(MouseButton button);

    /// rotation is the wxMouseEvent wheel rotation and delta the amount of rotation that makes up one wheel notch.
    void handleMouseWheel(int rotation, int delta);

    /// keyCode is a wxWidgets key code. Both return true when the key was consumed.
    bool handleKeyDown(int keyCode, bool altDown);

    bool handleKeyUp(int keyCode);

    /// Releases every held movement key, for when the canvas loses the keyboard and would miss the key-up events.
    void handleKeyboardFocusLost();

    bool isModifierKeyDown() const {
        return modifierKeyDown;
    }

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

private:
    /// Wheel notches scrolled over the menu since the last frame; moved into mouseScroll by handleInput.
    int pendingMouseScroll;
    /// Fraction of a wheel notch accumulated so far, for devices that report partial notches.
    float wheelNotches;
    bool modifierKeyDown;
    /// One bit per entry of the movement key table in InputHandler.cpp, set while that key is held down.
    unsigned int pressedMovementKeys;
};
