#pragma once

#include <SDL_syswm.h>
#include "../../NavWeakness/Vec3.h"
#include "../util/Math.h"
class FTPixmapFont;

typedef double GLdouble;
typedef int GLint;

enum HitTestType {
    NONE = 0,
    NAVMESH_AREA = 240,
    AIRG_WAYPOINT = 241,
    PF_SEED_POINT = 242,
    PF_EXCLUSION_BOX = 243
};

class HitTestResult {
public:
    HitTestResult(HitTestType type, int selectedIndex): type(type), selectedIndex(selectedIndex) {
    }

    HitTestType type;
    int selectedIndex;
};

class Renderer {
public:
    Renderer();

    ~Renderer();

    void handleMoved();

    static Renderer &getInstance() {
        static Renderer instance;
        return instance;
    }

    void initFrameBuffer(int width, int height);

    void drawText(const std::string &text, Vec3 pos, Vec3 color = {0.0, 0.0, 0.0}, double size = 32.0);

    void drawBox(Vec3 pos, Vec3 size, Math::Quaternion rotation, bool filled, Vec3 fillColor, bool outlined,
                 Vec3 outlineColor, float alpha);

    bool initWindowAndRenderer();

    void closeWindow() const;

    void loadSettings();

    void initFrameRate(float frameRateValue);

    void updateFrameRate();

    void handleResize();

    void renderFrame();

    void finalizeFrame() const;

    static void drawBounds();

    void drawAxes();

    HitTestResult hitTestRender(int mx, int my) const;

    unsigned int framebuffer;
    unsigned int color_rb;
    unsigned int depth_rb;
    FTPixmapFont *font;
    int width;
    int height;
    float frameRate;
    float initialFrameRate;
    float cameraEulers[2];
    float cameraPos[3];
    float camr;
    float origCameraEulers[2];
    GLdouble projectionMatrix[16];
    GLdouble modelviewMatrix[16];
    GLint viewport[4];

    SDL_Window *window;
    static HWND hwnd;
    Uint32 prevFrameTime;
};
