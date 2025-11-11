#include <FTGL/ftgl.h>
#include "../../include/NavKit/Resource.h"
#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/Obj.h"
#include "../../include/NavKit/module/Renderer.h"

#include <numbers>
#include <array>

#include "../../include/NavKit/module/Scene.h"
#include "../../include/NavKit/module/Settings.h"
#include "../../include/RecastDemo/imguiRenderGL.h"
#include <SDL.h>
#include <GL/glew.h>
#include <GL/glu.h>
#include <GL/glew.h>
#include <SDL_opengl.h>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/util/Math.h"

HWND Renderer::hwnd = nullptr;

Renderer::Renderer() {
    framebuffer = 0;
    color_rb = 0;
    depth_rb = 0;
    height = 0;
    width = 0;
    initialFrameRate = 60.0f;
    frameRate = 60.0f;
    window = 0;

    cameraEulers[0] = 45.0, cameraEulers[1] = -45.0;
    cameraPos[0] = 10, cameraPos[1] = 15, cameraPos[2] = 10;
    camr = 1000;
    origCameraEulers[0] = 0, origCameraEulers[1] = 0; // Used to compute rotational changes across frames.
    prevFrameTime = 0;
    font = new FTGLPixmapFont("DroidSans.ttf");
}

Renderer::~Renderer() {
    delete font;
    imguiRenderGLDestroy();
    SDL_Quit();
}

void Renderer::initFrameBuffer(const int width, const int height) {
    glewInit();
    // Build the framebuffer.
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    glGenRenderbuffers(1, &color_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rb);

    glGenRenderbuffers(1, &depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    updateFrameRate();
}

void Renderer::initFrameRate(float frameRateValue) {
    initialFrameRate = frameRateValue;
}

void Renderer::updateFrameRate() {
    if (initialFrameRate == -1.00000000) {
        const int displayIndex = SDL_GetWindowDisplayIndex(window);
        SDL_DisplayMode displayMode;
        if (!SDL_GetCurrentDisplayMode(displayIndex, &displayMode)) {
            frameRate = static_cast<float>(displayMode.refresh_rate);
        } else {
            frameRate = 60.0f;
        }
    }
    Logger::log(NK_INFO, ("Setting framerate to " + std::to_string(frameRate)).c_str());
}

bool Renderer::initWindowAndRenderer() {
    // Init SDL
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        printf("Could not initialise SDL.\nError: %s\n", SDL_GetError());
        return false;
    }

    // Use OpenGL render driver.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

    // Enable depth buffer.
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 12);

    // Set color channel depth.
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 2);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 2);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 2);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 2);

    // 4x MSAA.
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_RENDERER_ACCELERATED;
    constexpr float aspect = 16.0f / 9.0f;
    width = std::min(displayMode.w, static_cast<int>(static_cast<float>(displayMode.h) * aspect)) - 120;
    height = displayMode.h - 120;
    window = SDL_CreateWindow("", 0, 0, width, height, flags);
    if (const auto context = SDL_GL_CreateContext(window); context == nullptr) {
        printf("Could not initialise SDL.\nError: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 1024, 768);
    initFrameBuffer(width, height);

    if (!window) {
        printf("Could not initialise SDL opengl\nError: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    const std::string navKitVersion = NavKit_VERSION_MAJOR "." NavKit_VERSION_MINOR "." NavKit_VERSION_PATCH;
    std::string title = "NavKit ";
    title += navKitVersion;
    SDL_SetWindowTitle(window, title.data());

    if (!imguiRenderGLInit("DroidSans.ttf")) {
        printf("Could not init GUI renderer.\n");
        SDL_Quit();
        return false;
    }

    // Fog.
    Settings &settings = Settings::getInstance();
    const float backgroundColor = settings.backgroundColor;
    float fogColor[4] = {backgroundColor, backgroundColor, backgroundColor, 1.0f};
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, camr * 0.1f);
    glFogf(GL_FOG_END, camr * 1.25f);
    glFogfv(GL_FOG_COLOR, fogColor);

    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glClearColor(backgroundColor, backgroundColor, backgroundColor, 1.0f);
    prevFrameTime = SDL_GetTicks();

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo)) {
        Logger::log(NK_ERROR, "Could not get window manager info.");
        return false;
    }
    hwnd = wmInfo.info.win.window;
    HINSTANCE hInstance = wmInfo.info.win.hinstance;

    if (HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_NAVKITMENU))) {
        SetMenu(hwnd, hMenu);
    } else {
        Logger::log(NK_ERROR, "Failed to load menu resource.");
    }

    return true;
}

void Renderer::closeWindow() const {
    SDL_DestroyWindow(window);
}

void Renderer::handleResize() {
    width = SDL_GetWindowSurface(window)->w;
    height = SDL_GetWindowSurface(window)->h;
    Logger::log(NK_INFO,
                ("Window resized. New dimensions: " + std::to_string(width) + "x" + std::to_string(height)).c_str());
}

void Renderer::renderFrame() {
    // Set the viewport.
    glViewport(0, 0, width, height);
    glGetIntegerv(GL_VIEWPORT, viewport);

    // Clear the screen
    Settings &settings = Settings::getInstance();
    float backgroundColor = settings.backgroundColor;
    glClearColor(backgroundColor, backgroundColor, backgroundColor, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);

    // Compute the projection matrix.
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50.0f, static_cast<float>(width) / static_cast<float>(height), 1.0f, camr);

    glGetDoublev(GL_PROJECTION_MATRIX, projectionMatrix);

    // Compute the modelview matrix.
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(cameraEulers[0], 1, 0, 0);
    glRotatef(cameraEulers[1], 0, 1, 0);
    glTranslatef(-cameraPos[0], -cameraPos[1], -cameraPos[2]);
    glGetDoublev(GL_MODELVIEW_MATRIX, modelviewMatrix);
    const Uint32 time = SDL_GetTicks();
    const float dt = static_cast<float>(time - prevFrameTime) / 1000.0f;
    prevFrameTime = time;

    // Clamp the framerate so that we do not hog all the CPU.
    if (const float MIN_FRAME_TIME = 1.0f / frameRate; dt < MIN_FRAME_TIME) {
        int ms = static_cast<int>((MIN_FRAME_TIME - dt) * 1000.0f);
        if (ms > 10) ms = 10;
        if (ms >= 0) SDL_Delay(ms);
    }
    InputHandler::getInstance().handleMovement(dt, modelviewMatrix);

    glFrontFace(GL_CW);
    Grid grid = Grid::getInstance();
    if (grid.showGrid) {
        grid.renderGrid();
    }
    if (Navp &navp = Navp::getInstance(); navp.navpLoaded && navp.showNavp) {
        navp.renderNavMesh();
    }
    RecastAdapter &recastAdapter = RecastAdapter::getInstance();
    if (Navp &navp = Navp::getInstance(); navp.navpLoaded && navp.showRecastDebugInfo) {
        recastAdapter.renderRecastNavmesh(false);
    }
    Scene &scene = Scene::getInstance();
    if (Navp &navp = Navp::getInstance(); navp.showPfExclusionBoxes && scene.sceneLoaded) {
        navp.renderExclusionBoxes();
    }
    if (Navp &navp = Navp::getInstance(); navp.showPfSeedPoints && scene.sceneLoaded) {
        navp.renderPfSeedPoints();
    }
    if (Airg &airg = Airg::getInstance(); airg.airgLoaded && airg.showAirg) {
        airg.renderAirg();
    }
    if (Airg &airg = Airg::getInstance(); airg.airgLoaded && airg.showRecastDebugInfo) {
        RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
        recastAirgAdapter.renderRecastNavmesh(true);
    }
    if (Obj &obj = Obj::getInstance(); obj.objLoaded && obj.showObj) {
        obj.renderObj();
    }
    // Marker
    GLdouble x, y, z;
    if (recastAdapter.markerPositionSet) {
        // Draw marker circle
        glLineWidth(5.0f);
        glColor4f(240, 220, 0, 196);
        glBegin(GL_LINE_LOOP);
        const float r = 0.5f;
        for (int i = 0; i < 20; ++i) {
            const float a = (float) i / 20.0f * std::numbers::pi * 2;
            glVertex3f(recastAdapter.markerPosition[0] + cosf(a) * r,
                       (GLdouble) recastAdapter.markerPosition[1],
                       (GLdouble) recastAdapter.markerPosition[2] + sinf(a) * r);
        }
        glEnd();
        glLineWidth(1.0f);
    }
    if (grid.showGrid) {
        grid.renderGridText();
    }
    glClear(GL_DEPTH_BUFFER_BIT);
    drawBounds();
    drawAxes();
}

void Renderer::finalizeFrame() const {
    glEnable(GL_DEPTH_TEST);
    SDL_GL_SwapWindow(window);
}

void drawLine(const Vec3 s, const Vec3 e, const Vec3 color = {-1, -1, -1}) {
    if (color.X != -1) {
        glColor3f(color.X, color.Y, color.Z);
    }
    glDepthMask(GL_FALSE);
    glBegin(GL_LINES);
    glVertex3f(s.X, s.Y, s.Z);
    glVertex3f(e.X, e.Y, e.Z);
    glEnd();
    glDepthMask(GL_TRUE);
}

void Renderer::drawBounds() const {
    glDepthMask(GL_FALSE);
    Navp &navp = Navp::getInstance();
    float *p = navp.bBoxPos;
    float *s = navp.bBoxScale;
    float l = p[0] - s[0] / 2;
    float r = p[0] + s[0] / 2;
    float u = p[2] + s[2] / 2;
    float d = p[2] - s[2] / 2;
    float f = p[1] - s[1] / 2;
    float b = p[1] + s[1] / 2;
    Vec3 lfu = {l, f, u};
    Vec3 rfu = {r, f, u};
    Vec3 lbu = {l, b, u};
    Vec3 rbu = {r, b, u};
    Vec3 lfd = {l, f, d};
    Vec3 rfd = {r, f, d};
    Vec3 lbd = {l, b, d};
    Vec3 rbd = {r, b, d};
    glColor3f(0.0, 1.0, 1.0);
    drawLine(lfu, rfu);
    drawLine(lfu, lbu);
    drawLine(lfu, lfd);
    drawLine(rfu, rbu);
    drawLine(rfu, rfd);
    drawLine(lbu, rbu);
    drawLine(lbu, lbd);
    drawLine(rbu, rbd);
    drawLine(lfd, rfd);
    drawLine(lfd, lbd);
    drawLine(rfd, rbd);
    drawLine(lbd, rbd);
    glDepthMask(GL_TRUE);
}

void Renderer::drawAxes() {
    const Vec3 o = {0, 0, 0};
    const Vec3 x = {1, 0, 0};
    const Vec3 y = {0, 1, 0};
    const Vec3 z = {0, 0, 1};

    drawLine(o, x, x);
    drawText("X", x, x);

    drawLine(o, y, y);
    drawText("Y", y, y);

    drawLine(o, z * -1, z);
    drawText("Z", z * -1, z);
}

void Renderer::drawText(const std::string &text, const Vec3 pos, const Vec3 color, const double size) {
    if (font->Error())
        return;

    Vec3 camPos{cameraPos[0], cameraPos[1], cameraPos[2]};
    float distance = camPos.DistanceTo(pos);
    float fontSize = 10 * size / (distance != 0 ? distance : 0.001);
    if (fontSize < 15) {
        return;
    }
    glColor3f(color.X, color.Y, color.Z);

    font->FaceSize(fontSize);
    glRasterPos3f(pos.X, pos.Y, pos.Z);
    font->Render(text.c_str());
}

void Renderer::drawBox(Vec3 pos, Vec3 size, Math::Quaternion rotation, bool filled, Vec3 fillColor, bool outlined,
                       Vec3 outlineColor, float alpha) {
    // Bottom face
    Vec3 rotated1 = rotatePoint({-size.X / 2, -size.Y / 2, -size.Z / 2}, rotation);
    Vec3 rotated2 = rotatePoint({-size.X / 2, +size.Y / 2, -size.Z / 2}, rotation);
    Vec3 rotated3 = rotatePoint({+size.X / 2, +size.Y / 2, -size.Z / 2}, rotation);
    Vec3 rotated4 = rotatePoint({+size.X / 2, -size.Y / 2, -size.Z / 2}, rotation);
    if (filled) {
        glColor4f(fillColor.X, fillColor.Y, fillColor.Z, alpha);
        glBegin(GL_POLYGON);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    if (outlined) {
        glColor4f(outlineColor.X, outlineColor.Y, outlineColor.Z, alpha);
        glBegin(GL_LINE_LOOP);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    // Top face
    rotated1 = rotatePoint({-size.X / 2, -size.Y / 2, +size.Z / 2}, rotation);
    rotated2 = rotatePoint({-size.X / 2, +size.Y / 2, +size.Z / 2}, rotation);
    rotated3 = rotatePoint({+size.X / 2, +size.Y / 2, +size.Z / 2}, rotation);
    rotated4 = rotatePoint({+size.X / 2, -size.Y / 2, +size.Z / 2}, rotation);
    if (filled) {
        glColor4f(fillColor.X, fillColor.Y, fillColor.Z, alpha);
        glBegin(GL_POLYGON);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    if (outlined) {
        glColor4f(outlineColor.X, outlineColor.Y, outlineColor.Z, alpha);
        glBegin(GL_LINE_LOOP);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    // Left face
    rotated1 = rotatePoint({-size.X / 2, -size.Y / 2, -size.Z / 2}, rotation);
    rotated2 = rotatePoint({-size.X / 2, -size.Y / 2, +size.Z / 2}, rotation);
    rotated3 = rotatePoint({-size.X / 2, +size.Y / 2, +size.Z / 2}, rotation);
    rotated4 = rotatePoint({-size.X / 2, +size.Y / 2, -size.Z / 2}, rotation);
    if (filled) {
        glColor4f(fillColor.X, fillColor.Y, fillColor.Z, alpha);
        glBegin(GL_POLYGON);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    if (outlined) {
        glColor4f(outlineColor.X, outlineColor.Y, outlineColor.Z, alpha);
        glBegin(GL_LINE_LOOP);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    // Right face
    rotated1 = rotatePoint({+size.X / 2, -size.Y / 2, -size.Z / 2}, rotation);
    rotated2 = rotatePoint({+size.X / 2, -size.Y / 2, +size.Z / 2}, rotation);
    rotated3 = rotatePoint({+size.X / 2, +size.Y / 2, +size.Z / 2}, rotation);
    rotated4 = rotatePoint({+size.X / 2, +size.Y / 2, -size.Z / 2}, rotation);
    if (filled) {
        glColor4f(fillColor.X, fillColor.Y, fillColor.Z, alpha);
        glBegin(GL_POLYGON);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    if (outlined) {
        glColor4f(outlineColor.X, outlineColor.Y, outlineColor.Z, alpha);
        glBegin(GL_LINE_LOOP);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    // Front face
    rotated1 = rotatePoint({-size.X / 2, -size.Y / 2, -size.Z / 2}, rotation);
    rotated2 = rotatePoint({-size.X / 2, -size.Y / 2, +size.Z / 2}, rotation);
    rotated3 = rotatePoint({+size.X / 2, -size.Y / 2, +size.Z / 2}, rotation);
    rotated4 = rotatePoint({+size.X / 2, -size.Y / 2, -size.Z / 2}, rotation);
    if (filled) {
        glColor4f(fillColor.X, fillColor.Y, fillColor.Z, alpha);
        glBegin(GL_POLYGON);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    if (outlined) {
        glColor4f(outlineColor.X, outlineColor.Y, outlineColor.Z, alpha);
        glBegin(GL_LINE_LOOP);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    // Back face
    rotated1 = rotatePoint({-size.X / 2, +size.Y / 2, -size.Z / 2}, rotation);
    rotated2 = rotatePoint({-size.X / 2, +size.Y / 2, +size.Z / 2}, rotation);
    rotated3 = rotatePoint({+size.X / 2, +size.Y / 2, +size.Z / 2}, rotation);
    rotated4 = rotatePoint({+size.X / 2, +size.Y / 2, -size.Z / 2}, rotation);
    if (filled) {
        glColor4f(fillColor.X, fillColor.Y, fillColor.Z, alpha);
        glBegin(GL_POLYGON);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
    if (outlined) {
        glColor4f(outlineColor.X, outlineColor.Y, outlineColor.Z, alpha);
        glBegin(GL_LINE_LOOP);
        glVertex3f(pos.X + rotated1.X, pos.Y + rotated1.Y, pos.Z + rotated1.Z);
        glVertex3f(pos.X + rotated2.X, pos.Y + rotated2.Y, pos.Z + rotated2.Z);
        glVertex3f(pos.X + rotated3.X, pos.Y + rotated3.Y, pos.Z + rotated3.Z);
        glVertex3f(pos.X + rotated4.X, pos.Y + rotated4.Y, pos.Z + rotated4.Z);
        glEnd();
    }
}

HitTestResult Renderer::hitTestRender(const int mx, const int my) const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glClearColor(1.0, 1.0, 1.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        Logger::log(NK_ERROR, ("FB error, status: 0x" + std::to_string((int) status)).c_str());

        printf("FB error, status: 0x%x\n", status);
        return HitTestResult(NONE, -1);
    }
    Navp::getInstance().renderNavMeshForHitTest();
    Navp::getInstance().renderPfSeedPointsForHitTest();
    Navp::getInstance().renderExclusionBoxesForHitTest();
    Airg::getInstance().renderAirgForHitTest();
    Obj &obj = Obj::getInstance();
    if (obj.showObj && obj.objLoaded) {
        obj.renderObj();
    }
    GLubyte pixel[4];
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(mx, my, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    int selectedIndex = int(pixel[1]) * 255 + int(pixel[2]);

    if (selectedIndex == 65280) {
        return HitTestResult(NONE, -1);
    }
    HitTestType result = NONE;
    if (int(pixel[0]) == NAVMESH_AREA) {
        result = NAVMESH_AREA;
    } else if (int(pixel[0]) == AIRG_WAYPOINT) {
        result = AIRG_WAYPOINT;
    } else if (int(pixel[0]) == PF_SEED_POINT) {
        result = PF_SEED_POINT;
    } else if (int(pixel[0]) == PF_EXCLUSION_BOX) {
        result = PF_EXCLUSION_BOX;
    }
    return HitTestResult(result, result != NONE ? selectedIndex : -1);
}
