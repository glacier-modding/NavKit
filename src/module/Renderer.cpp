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
#include "../../include/RecastDemo/imguiRenderGL.h"
#include <SDL.h>

#include <GL/glew.h>
#include <GL/glu.h>

#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/util/Math.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

HWND Renderer::hwnd = nullptr;

Renderer::Renderer() : projectionMatrix{}, modelviewMatrix{}, viewport{} {
    framebuffer = 0;
    color_rb = 0;
    depth_rb = 0;
    height = 0;
    width = 0;
    initialFrameRate = 60.0f;
    frameRate = 60.0f;
    window = 0;

    cameraEulers[0] = 45.0, cameraEulers[1] = 135.0;
    cameraPos[0] = 10, cameraPos[1] = 15, cameraPos[2] = 10;
    camr = 1000;
    origCameraEulers[0] = 0,
    origCameraEulers[1] = 0;
    prevFrameTime = 0;
    font = new FTGLPolygonFont("DroidSans.ttf");
    font->FaceSize(72);
}

Renderer::~Renderer() {
    delete font;
    imguiRenderGLDestroy();
    SDL_Quit();
}

void Renderer::initFrameBuffer(const int width, const int height) {
    glewInit();
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

void Renderer::initFrameRate(const float frameRateValue) {
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
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        printf("Could not initialise SDL.\nError: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 2);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 2);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 2);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 2);

    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    constexpr Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_RENDERER_ACCELERATED;
    const PersistedSettings &persistedSettings = PersistedSettings::getInstance();
    if (const float
                settingsWidth = atof(persistedSettings.getValue("Renderer", "windowWidth", "-1.0f")),
                settingsHeight = atof(persistedSettings.getValue("Renderer", "windowHeight", "-1.0f"));
        settingsWidth == -1.0f || settingsHeight == -1) {
        constexpr float aspect = 16.0f / 9.0f;
        width = std::min(displayMode.w, static_cast<int>(static_cast<float>(displayMode.h) * aspect)) - 120;
        height = displayMode.h - 120;
    } else {
        width = settingsWidth;
        height = settingsHeight;
    }
    window = SDL_CreateWindow("", 0, 0, width, height, flags);
    if (const auto context = SDL_GL_CreateContext(window); context == nullptr) {
        printf("Could not initialise SDL.\nError: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 200, 100);
    initFrameBuffer(width, height);

    if (!window) {
        printf("Could not initialise SDL opengl\nError: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    const float x = atof(persistedSettings.getValue("Renderer", "windowX", "-1.0f")),
            y = atof(persistedSettings.getValue("Renderer", "windowY", "-1.0f"));
    if (x == -1.0f || y == -1.0f) {
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    } else {
        SDL_SetWindowPosition(window, x, y);
    }
    const std::string navKitVersion = NavKit_VERSION_MAJOR "." NavKit_VERSION_MINOR "." NavKit_VERSION_PATCH;
    std::string title = "NavKit ";
    title += navKitVersion;
    SDL_SetWindowTitle(window, title.data());

    if (!imguiRenderGLInit("DroidSans.ttf")) {
        printf("Could not init GUI renderer.\n");
        SDL_Quit();
        return false;
    }

    NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    const float backgroundColor = navKitSettings.backgroundColor;
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

void Renderer::loadSettings() {
    const PersistedSettings &persistedSettings = PersistedSettings::getInstance();
    initFrameRate(static_cast<float>(atof(persistedSettings.getValue("Renderer", "frameRate", "-1.0f"))));
}

void Renderer::handleMoved() {
    updateFrameRate();
    PersistedSettings &persistedSettings = PersistedSettings::getInstance();
    int x;
    int y;
    SDL_GetWindowPosition(window, &x, &y);
    persistedSettings.setValue("Renderer", "windowX", std::to_string(x));
    persistedSettings.setValue("Renderer", "windowY", std::to_string(y));
    persistedSettings.setValue("Renderer", "frameRate", std::to_string(frameRate));
    persistedSettings.save();
}

void Renderer::initShaders() {
    shader = Shader();
    shader.loadShaders("vertex.glsl", "fragment.glsl");
}

void Renderer::handleResize() {
    width = SDL_GetWindowSurface(window)->w;
    height = SDL_GetWindowSurface(window)->h;
    Logger::log(NK_INFO,
                ("Window resized. New dimensions: " + std::to_string(width) + "x" + std::to_string(height)).c_str());
    PersistedSettings &persistedSettings = PersistedSettings::getInstance();
    persistedSettings.setValue("Renderer", "windowWidth", std::to_string(width));
    persistedSettings.setValue("Renderer", "windowHeight", std::to_string(height));
    persistedSettings.setValue("Renderer", "frameRate", std::to_string(frameRate));
    persistedSettings.save();
}

void Renderer::renderFrame() {
    glViewport(0, 0, width, height);
    glGetIntegerv(GL_VIEWPORT, viewport);

    NavKitSettings &navKitSettings = NavKitSettings::getInstance();
    float backgroundColor = navKitSettings.backgroundColor;
    glClearColor(backgroundColor, backgroundColor, backgroundColor, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);

    projection = glm::perspective(glm::radians(50.0f), static_cast<float>(width) / static_cast<float>(height), 1.0f, camr);
    view = glm::lookAt(glm::vec3(cameraPos[0], cameraPos[1], cameraPos[2]),
                       glm::vec3(cameraPos[0] + -sin(glm::radians(cameraEulers[1])) * cos(glm::radians(cameraEulers[0])),
                                 cameraPos[1] + -sin(glm::radians(cameraEulers[0])),
                                 cameraPos[2] + cos(glm::radians(cameraEulers[1])) * cos(glm::radians(cameraEulers[0]))),
                       glm::vec3(0.0f, 1.0f, 0.0f));

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50.0f, static_cast<float>(width) / static_cast<float>(height), 1.0f, camr);

    glGetDoublev(GL_PROJECTION_MATRIX, projectionMatrix);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(cameraPos[0], cameraPos[1], cameraPos[2],
              cameraPos[0] + -sin(glm::radians(cameraEulers[1])) * cos(glm::radians(cameraEulers[0])),
              cameraPos[1] + -sin(glm::radians(cameraEulers[0])),
              cameraPos[2] + cos(glm::radians(cameraEulers[1])) * cos(glm::radians(cameraEulers[0])),
              0.0, 1.0, 0.0);
    glGetDoublev(GL_MODELVIEW_MATRIX, modelviewMatrix);
    const Uint32 time = SDL_GetTicks();
    const float dt = static_cast<float>(time - prevFrameTime) / 1000.0f;
    prevFrameTime = time;

    if (const float MIN_FRAME_TIME = 1.0f / frameRate; dt < MIN_FRAME_TIME) {
        int ms = static_cast<int>((MIN_FRAME_TIME - dt) * 1000.0f);
        if (ms > 10) ms = 10;
        if (ms >= 0) SDL_Delay(ms);
    }
    InputHandler::getInstance().handleMovement(dt, modelviewMatrix);

    glFrontFace(GL_CW);

    // Reset shader state to ensure objects render with lighting/textures by default
    shader.use();
    shader.setBool("useFlatColor", false);
    shader.setBool("useVertexColor", false);
    glUseProgram(0);

    if (const Obj &obj = Obj::getInstance(); obj.objLoaded && obj.showObj) {
        obj.renderObj();
        glUseProgram(0);
        glDisable(GL_CULL_FACE);
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
    if (const Airg &airg = Airg::getInstance(); airg.airgLoaded && airg.showRecastDebugInfo) {
        RecastAdapter &recastAirgAdapter = RecastAdapter::getAirgInstance();
        recastAirgAdapter.renderRecastNavmesh(true);
    }

    Grid grid = Grid::getInstance();
    if (grid.showGrid) {
        grid.renderGrid();
    }
    if (recastAdapter.markerPositionSet) {
        glLineWidth(5.0f);
        glColor4f(240, 220, 0, 196);
        glBegin(GL_LINE_LOOP);
        const float r = 0.5f;
        for (int i = 0; i < 20; ++i) {
            const float a = static_cast<float>(i) / 20.0f * std::numbers::pi * 2;
            glVertex3f(recastAdapter.markerPosition[0] + cosf(a) * r,
                       recastAdapter.markerPosition[1],
                       (GLdouble) recastAdapter.markerPosition[2] + sinf(a) * r);
        }
        glEnd();
        glLineWidth(1.0f);
    }
    if (grid.showGrid) {
        grid.renderGridText();
    }

    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    if (scene.showBBox) {
        drawBounds();
    }
    drawAxes();
    glEnable(GL_DEPTH_TEST);
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

void Renderer::drawBounds() {
    glDepthMask(GL_FALSE);
    Scene &scene = Scene::getInstance();
    float *p = scene.bBoxPos;
    float *s = scene.bBoxScale;
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

    glPushMatrix();
    glTranslatef(pos.X, pos.Y, pos.Z);

    float modelview[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelview);
    for(int i=0; i<3; i++) 
        for(int j=0; j<3; j++) 
            modelview[i*4+j] = (i==j ? 1.0f : 0.0f);
    glLoadMatrixf(modelview);

    float scale = static_cast<float>(size) * 0.0002f;
    glScalef(scale, scale, scale);

    glColor3f(color.X, color.Y, color.Z);
    font->Render(text.c_str());

    glPopMatrix();
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
