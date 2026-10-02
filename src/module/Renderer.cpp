#include "../../include/NavKit/NavKitConfig.h"
#include "../../include/NavKit/module/Airg.h"
#include "../../include/NavKit/module/Grid.h"
#include "../../include/NavKit/module/InputHandler.h"
#include "../../include/NavKit/module/Logger.h"
#include "../../include/NavKit/module/Navp.h"
#include "../../include/NavKit/module/SceneMesh.h"
#include "../../include/NavKit/module/Renderer.h"
#include "../../include/NavKit/render/CoreProfileDraw.h"

#include <numbers>
#include <array>
#include <vector>

#include "../../include/NavKit/module/Scene.h"
#include "../../include/RecastDemo/imguiRenderGL.h"
#include <SDL.h>

#include <GL/glew.h>
#include "../../include/NavKit/adapter/RecastAdapter.h"
#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/PersistedSettings.h"
#include "../../include/NavKit/module/WxApplication.h"
#include "../../include/NavKit/util/FileUtil.h"
#include "../../include/NavKit/util/Math.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

Renderer::Renderer() : projectionMatrix{}, modelviewMatrix{}, viewport{} {
    framebuffer = 0;
    color_rb = 0;
    depth_rb = 0;
    height = 0;
    width = 0;
    initialFrameRate = 60.0f;
    frameRate = 60.0f;
    window = nullptr;

    cameraEulers[0] = 45.0, cameraEulers[1] = 135.0;
    cameraPos[0] = 10, cameraPos[1] = 15, cameraPos[2] = 10;
    camr = 10000;
    origCameraEulers[0] = 0, origCameraEulers[1] = 0;
    prevFrameTime = 0;
}

Renderer::~Renderer() {
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

    SDL_DisplayMode displayMode;
    SDL_GetCurrentDisplayMode(0, &displayMode);
    const PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    if (const float settingsWidth = atof(persistedSettings.getValue("Renderer", "windowWidth", "-1.0f")),
        settingsHeight = atof(persistedSettings.getValue("Renderer", "windowHeight", "-1.0f"));
        settingsWidth == -1.0f || settingsHeight == -1) {
        constexpr float aspect = 16.0f / 9.0f;
        width = std::min(displayMode.w, static_cast<int>(static_cast<float>(displayMode.h) * aspect)) - 120;
        height = displayMode.h - 120;
    } else {
        width = settingsWidth;
        height = settingsHeight;
    }
    wxWindow* renderPanel = getRenderPanel();
    if (!renderPanel) {
        Logger::log(NK_ERROR, "Could not create the wxWidgets render panel.");
        return false;
    }
    getMainFrame()->SetClientSize(width, height);
    getMainFrame()->Layout();
    handleResize();
    window = SDL_CreateWindowFrom(renderPanel->GetHandle());
    if (!window) {
        Logger::log(NK_ERROR, "Could not attach SDL to the wxWidgets render panel: %s", SDL_GetError());
        return false;
    }
    if (!initializeRenderContext()) {
        Logger::log(NK_ERROR, "Could not create or activate the wxWidgets OpenGL context.");
        return false;
    }
    getMainFrame()->SetMinSize(wxSize(200, 100));
    const char* modeStr = persistedSettings.getValue("Renderer", "fullscreen", "WINDOWED");
    if (strcmp(modeStr, "BORDERLESS_FULLSCREEN") == 0) {
        getMainFrame()->ShowFullScreen(true);
    } else if (strcmp(modeStr, "MAXIMIZED") == 0) {
        getMainFrame()->Maximize();
    } else {
        const float x = atof(persistedSettings.getValue("Renderer", "windowX", "-1.0f")),
                    y = atof(persistedSettings.getValue("Renderer", "windowY", "-1.0f"));
        if (x == -1.0f || y == -1.0f) {
            getMainFrame()->Centre();
        } else {
            getMainFrame()->Move(static_cast<int>(x), static_cast<int>(y));
        }
    }
    getMainFrame()->Hide();
    getMainFrame()->Layout();
    handleResize();
    initFrameBuffer(width, height);

    constexpr std::string_view navKitVersion = NavKit_VERSION_MAJOR "." NavKit_VERSION_MINOR "." NavKit_VERSION_PATCH;
    std::string title = "NavKit ";
    title += navKitVersion;
    getMainFrame()->SetTitle(title);

    const std::string fontPath = FileUtil::getApplicationResourcePath("DroidSans.ttf").string();
    if (!imguiRenderGLInit(fontPath.c_str())) {
        printf("Could not init GUI renderer.\n");
        SDL_Quit();
        return false;
    }

    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const float backgroundColor = navKitSettings.backgroundColor;
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glEnable(GL_BLEND);
    glClearColor(backgroundColor, backgroundColor, backgroundColor, 1.0f);
    prevFrameTime = SDL_GetTicks();

    return true;
}

void Renderer::closeWindow() const {
    SDL_DestroyWindow(window);
}

void Renderer::loadSettings() {
    const PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    initFrameRate(static_cast<float>(atof(persistedSettings.getValue("Renderer", "frameRate", "-1.0f"))));
}

void Renderer::handleMoved() {
    updateFrameRate();
    if (getMainFrame()->IsFullScreen() || getMainFrame()->IsMaximized()) {
        return;
    }
    PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    const wxPoint position = getMainFrame()->GetPosition();
    const int x = position.x;
    const int y = position.y;
    persistedSettings.setValue("Renderer", "windowX", std::to_string(x));
    persistedSettings.setValue("Renderer", "windowY", std::to_string(y));
    persistedSettings.setValue("Renderer", "frameRate", std::to_string(frameRate));
    persistedSettings.save();
}

void Renderer::handleFullscreen(const FullscreenMode mode) const {
    if (!window) {
        return;
    }
    PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    FullscreenMode currentMode = WINDOWED;
    if (const char* modeStr = persistedSettings.getValue("Renderer", "fullscreen", "WINDOWED");
        strcmp(modeStr, "BORDERLESS_FULLSCREEN") == 0) {
        currentMode = BORDERLESS_FULLSCREEN;
    } else if (strcmp(modeStr, "MAXIMIZED") == 0) {
        currentMode = MAXIMIZED;
    }
    if (currentMode == mode) {
        return;
    }

    std::string modeStr = "WINDOWED";
    switch (mode) {
    case BORDERLESS_FULLSCREEN:
        getMainFrame()->ShowFullScreen(true);
        modeStr = "BORDERLESS_FULLSCREEN";
        break;
    case MAXIMIZED:
        getMainFrame()->ShowFullScreen(false);
        getMainFrame()->Maximize();
        modeStr = "MAXIMIZED";
        break;
    case WINDOWED:
    default:
        const float x = atof(persistedSettings.getValue("Renderer", "windowX", "-1.0f")),
                    y = atof(persistedSettings.getValue("Renderer", "windowY", "-1.0f"));
        if (x == -1.0f || y == -1.0f) {
            getMainFrame()->Centre();
        } else {
            getMainFrame()->Move(static_cast<int>(x), static_cast<int>(y));
        }
        getMainFrame()->ShowFullScreen(false);
        getMainFrame()->Restore();
        break;
    }

    Logger::log(NK_INFO, (std::string("Setting fullscreen mode to: ") + modeStr).c_str());
    persistedSettings.setValue("Renderer", "fullscreen", modeStr);
    persistedSettings.save();
}

void Renderer::initShaders() {
    shader = Shader();
    const std::string vertexShaderPath = FileUtil::getApplicationResourcePath("vertex.glsl").string();
    const std::string fragmentShaderPath = FileUtil::getApplicationResourcePath("fragment.glsl").string();
    shader.loadShaders(vertexShaderPath.c_str(), fragmentShaderPath.c_str());
}

void Renderer::handleResize() {
    if (const wxGLCanvas* renderPanel = getRenderPanel()) {
        const wxSize size = renderPanel->GetClientSize();
        width = size.GetWidth();
        height = size.GetHeight();
    }
}

void Renderer::handleResizeFinished() {
    handleResize();
    Logger::log(
        NK_INFO, ("Window resized. New dimensions: " + std::to_string(width) + "x" + std::to_string(height)).c_str());
    PersistedSettings& persistedSettings = PersistedSettings::getInstance();
    persistedSettings.setValue("Renderer", "windowWidth", std::to_string(width));
    persistedSettings.setValue("Renderer", "windowHeight", std::to_string(height));
    persistedSettings.setValue("Renderer", "frameRate", std::to_string(frameRate));
    persistedSettings.save();
}

void Renderer::renderFrame() {
    if (!makeRenderContextCurrent()) {
        Logger::log(NK_ERROR, "Could not activate the wxWidgets OpenGL context.");
        return;
    }
    setCoreProfileFogEnabled(true);
    glViewport(0, 0, width, height);
    glGetIntegerv(GL_VIEWPORT, viewport);

    const NavKitSettings& navKitSettings = NavKitSettings::getInstance();
    const float backgroundColor = navKitSettings.backgroundColor;
    glClearColor(backgroundColor, backgroundColor, backgroundColor, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    projection =
        glm::perspective(glm::radians(50.0f), static_cast<float>(width) / static_cast<float>(height), 1.0f, camr);
    view = glm::lookAt(glm::vec3(cameraPos[0], cameraPos[1], cameraPos[2]),
        glm::vec3(cameraPos[0] + -sin(glm::radians(cameraEulers[1])) * cos(glm::radians(cameraEulers[0])),
            cameraPos[1] + -sin(glm::radians(cameraEulers[0])),
            cameraPos[2] + cos(glm::radians(cameraEulers[1])) * cos(glm::radians(cameraEulers[0]))),
        glm::vec3(0.0f, 1.0f, 0.0f));

    const float* projectionValues = glm::value_ptr(projection);
    const float* viewValues = glm::value_ptr(view);
    for (int i = 0; i < 16; ++i) {
        projectionMatrix[i] = projectionValues[i];
        modelviewMatrix[i] = viewValues[i];
    }
    const Uint32 time = SDL_GetTicks();
    const float dt = static_cast<float>(time - prevFrameTime) / 1000.0f;
    prevFrameTime = time;

    if (const float MIN_FRAME_TIME = 1.0f / frameRate; dt < MIN_FRAME_TIME) {
        int ms = static_cast<int>((MIN_FRAME_TIME - dt) * 1000.0f);
        if (ms > 10) {
            ms = 10;
        }
        if (ms >= 0) {
            SDL_Delay(ms);
        }
    }
    InputHandler::getInstance().handleMovement(dt, modelviewMatrix);

    glFrontFace(GL_CW);

    // Reset shader state to ensure objects render with lighting/textures by default
    shader.use();
    shader.setBool("useFlatColor", false);
    shader.setBool("useVertexColor", false);
    shader.setBool("useTexture", false);
    shader.setBool("useUiTexture", false);
    shader.setBool("useAlphaTexture", false);
    shader.setBool("useFog", true);
    shader.setVec4("fogColor", glm::vec4(backgroundColor, backgroundColor, backgroundColor, 1.0f));
    shader.setFloat("fogStart", camr * 0.1f);
    shader.setFloat("fogEnd", camr * 1.25f);
    glUseProgram(0);

    if (const SceneMesh& obj = SceneMesh::getInstance(); obj.objLoaded && obj.showObj) {
        GLboolean blendEnabled;
        glGetBooleanv(GL_BLEND, &blendEnabled);
        glPolygonOffset(-1.0f, -1.0f);
        obj.renderObj();
        glPolygonOffset(0.0f, 0.0f);

        glUseProgram(0);
        glDisable(GL_CULL_FACE);
    }
    if (Navp& navp = Navp::getInstance(); navp.navpLoaded && navp.showNavp) {
        navp.renderNavMesh();
        if (navp.showKdTree) {
            navp.renderKdTree();
        }
    }
    const RecastAdapter& recastAdapter = RecastAdapter::getInstance();
    if (const Navp& navp = Navp::getInstance(); navp.navpLoaded && navp.showRecastDebugInfo) {
        recastAdapter.renderRecastNavmesh(false);
    }
    const Scene& scene = Scene::getInstance();
    if (const Navp& navp = Navp::getInstance(); navp.showPfExclusionBoxes && scene.sceneLoaded) {
        navp.renderExclusionBoxes();
    }
    if (const Navp& navp = Navp::getInstance(); navp.showPfSeedPoints && scene.sceneLoaded) {
        navp.renderPfSeedPoints();
    }
    if (Airg& airg = Airg::getInstance(); airg.airgLoaded && airg.showAirg) {
        airg.renderAirg();
    }
    if (const Airg& airg = Airg::getInstance(); airg.airgLoaded && airg.showRecastDebugInfo) {
        const RecastAdapter& recastAirgAdapter = RecastAdapter::getAirgInstance();
        recastAirgAdapter.renderRecastNavmesh(true);
    }

    const Grid grid = Grid::getInstance();
    if (grid.showGrid) {
        grid.renderGrid();
    }
    if (recastAdapter.markerPositionSet) {
        const Vec3 markerColor = {1.0f, 1.0f, 0.0f};
        const float r = 0.5f;
        for (int i = 0; i < 20; ++i) {
            const float a = static_cast<float>(i) / 20.0f * std::numbers::pi * 2;
            const float nextA = static_cast<float>(i + 1) / 20.0f * std::numbers::pi * 2;
            drawLine({recastAdapter.markerPosition[0] + cosf(a) * r, recastAdapter.markerPosition[1],
                         recastAdapter.markerPosition[2] + sinf(a) * r},
                {recastAdapter.markerPosition[0] + cosf(nextA) * r, recastAdapter.markerPosition[1],
                    recastAdapter.markerPosition[2] + sinf(nextA) * r},
                shader, view, projection, markerColor);
        }
    }
    if (grid.showGrid) {
        grid.renderGridText();
    }

    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    if (scene.showBBox) {
        drawBounds();
    }
    if (scene.showAxes) {
        drawAxes();
    }
    glEnable(GL_DEPTH_TEST);
}

void Renderer::finalizeFrame() const {
    glEnable(GL_DEPTH_TEST);
    swapRenderBuffers();
}

void drawLine(const Vec3 s, const Vec3 e, Shader& shader, const glm::mat4& view, const glm::mat4& projection,
    const Vec3 color, const float alpha) {
    static GLuint vao = 0, vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    float vertices[] = {s.X, s.Y, s.Z, e.X, e.Y, e.Z};

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_DYNAMIC_DRAW);

    shader.use();
    shader.setBool("useFlatColor", true);
    shader.setBool("useVertexColor", false);
    shader.setBool("useUiTexture", false);
    shader.setBool("useAlphaTexture", false);
    glm::vec4 c = (color.X == -1) ? glm::vec4(1.0f) : glm::vec4(color.X, color.Y, color.Z, alpha);
    shader.setVec4("flatColor", c);
    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    shader.setMat4("model", glm::mat4(1.0f));

    glDepthMask(GL_FALSE);
    glBindVertexArray(vao);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glUseProgram(0);
}

void Renderer::drawBounds() {
    glDepthMask(GL_FALSE);
    Scene& scene = Scene::getInstance();
    const float* p = scene.bBoxPos;
    const float* s = scene.bBoxScale;
    const float l = p[0] - s[0] / 2;
    const float r = p[0] + s[0] / 2;
    const float u = p[2] + s[2] / 2;
    const float d = p[2] - s[2] / 2;
    const float f = p[1] - s[1] / 2;
    const float b = p[1] + s[1] / 2;
    const Vec3 lfu = {l, f, u};
    const Vec3 rfu = {r, f, u};
    const Vec3 lbu = {l, b, u};
    const Vec3 rbu = {r, b, u};
    const Vec3 lfd = {l, f, d};
    const Vec3 rfd = {r, f, d};
    const Vec3 lbd = {l, b, d};
    const Vec3 rbd = {r, b, d};
    const Vec3 cyan = {0.0f, 1.0f, 1.0f};

    // Offset the lines slightly so they don't Z-fight with the mesh they enclose
    glPolygonOffset(-1.0f, -1.0f);

    drawLine(lfu, rfu, shader, view, projection, cyan);
    drawLine(lfu, lbu, shader, view, projection, cyan);
    drawLine(lfu, lfd, shader, view, projection, cyan);
    drawLine(rfu, rbu, shader, view, projection, cyan);
    drawLine(rfu, rfd, shader, view, projection, cyan);
    drawLine(lbu, rbu, shader, view, projection, cyan);
    drawLine(lbu, lbd, shader, view, projection, cyan);
    drawLine(rbu, rbd, shader, view, projection, cyan);
    drawLine(lfd, rfd, shader, view, projection, cyan);
    drawLine(lfd, lbd, shader, view, projection, cyan);
    drawLine(rfd, rbd, shader, view, projection, cyan);
    drawLine(lbd, rbd, shader, view, projection, cyan);

    glPolygonOffset(0.0f, 0.0f);
    glDepthMask(GL_TRUE);
}

void Renderer::drawAxes() {
    const Vec3 o = {0, 0, 0};
    const Vec3 x = {1, 0, 0};
    const Vec3 y = {0, 1, 0};
    const Vec3 z = {0, 0, 1};

    drawLine(o, x, shader, view, projection, x);
    drawText("X", x, x);

    drawLine(o, y, shader, view, projection, y);
    drawText("Y", y, y);

    drawLine(o, z * -1, shader, view, projection, z);
    drawText("Z", z * -1, z);
}

void Renderer::drawText(const std::string& text, const Vec3 pos, const Vec3 color, const double size) const {
    imguiRenderGLDrawWorldText(text.c_str(), pos.X, pos.Y, pos.Z, color.X, color.Y, color.Z, static_cast<float>(size));
}

void Renderer::drawBox(const Vec3 pos, const Vec3 size, const Math::Quaternion rotation, const bool filled,
    const Vec3 fillColor, const bool outlined, const Vec3 outlineColor, const float alpha) const {
    static GLuint filledVao = 0, filledVbo = 0;
    static GLuint outlineVao = 0, outlineVbo = 0;

    if (filledVao == 0) {
        constexpr float unitCube[] = {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
            0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f,
            -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f,
            0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
            0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f,
            -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, -0.5f,
            0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f};
        glGenVertexArrays(1, &filledVao);
        glGenBuffers(1, &filledVbo);
        glBindVertexArray(filledVao);
        glBindBuffer(GL_ARRAY_BUFFER, filledVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(unitCube), unitCube, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), static_cast<void*>(nullptr));

        constexpr float unitOutline[] = {-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
            0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f,
            -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
            -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
            0.5f, -0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f};
        glGenVertexArrays(1, &outlineVao);
        glGenBuffers(1, &outlineVbo);
        glBindVertexArray(outlineVao);
        glBindBuffer(GL_ARRAY_BUFFER, outlineVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(unitOutline), unitOutline, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), static_cast<void*>(nullptr));
        glBindVertexArray(0);
    }

    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.X, pos.Y, pos.Z));
    const glm::quat q(rotation.w, rotation.x, rotation.y, rotation.z);
    model = model * glm::mat4_cast(q);
    model = glm::scale(model, glm::vec3(size.X, size.Y, size.Z));

    shader.use();
    shader.setBool("useFlatColor", true);
    shader.setBool("useVertexColor", false);
    shader.setBool("useUiTexture", false);
    shader.setBool("useAlphaTexture", false);
    shader.setMat4("projection", projection);
    shader.setMat4("view", view);
    shader.setMat4("model", model);

    glPolygonOffset(-2.0f, -2.0f);

    if (filled) {
        glBindVertexArray(filledVao);
        shader.setVec4("flatColor", glm::vec4(fillColor.X, fillColor.Y, fillColor.Z, alpha));
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    if (outlined) {
        glBindVertexArray(outlineVao);
        shader.setVec4("flatColor", glm::vec4(outlineColor.X, outlineColor.Y, outlineColor.Z, alpha));
        glDrawArrays(GL_LINES, 0, 24);
    }

    glPolygonOffset(0.0f, 0.0f);
    glBindVertexArray(0);
    glUseProgram(0);
}

HitTestResult Renderer::hitTestRender(const int mx, const int my) const {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glClearColor(1.0, 1.0, 1.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    getInstance().renderFrame();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    if (const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER); status != GL_FRAMEBUFFER_COMPLETE) {
        Logger::log(NK_ERROR, ("FB error, status: 0x" + std::to_string(static_cast<int>(status))).c_str());

        printf("FB error, status: 0x%x\n", status);
        return {NONE, -1};
    }
    shader.use();
    shader.setBool("useUiTexture", false);
    shader.setBool("useAlphaTexture", false);
    shader.setBool("useFog", false);
    shader.setBool("useFlatColor", true);
    shader.setBool("useTexture", false);
    Navp::getInstance().renderNavMeshForHitTest();
    Navp::getInstance().renderPfSeedPointsForHitTest();
    Navp::getInstance().renderExclusionBoxesForHitTest();
    Airg::getInstance().renderAirgForHitTest();
    const SceneMesh& sceneMesh = SceneMesh::getInstance();
    if (sceneMesh.showObj && sceneMesh.objLoaded) {
        RecastAdapter::getInstance().renderRecastNavmesh(false);
    }
    GLubyte pixel[4];
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(mx, my, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);

    if (pixel[0] == 255) {
        return {NONE, -1};
    }

    const int selectedIndex = static_cast<int>(pixel[1]) * 256 + static_cast<int>(pixel[2]);
    HitTestType result = NONE;
    if (pixel[0] == NAVMESH_AREA) {
        result = NAVMESH_AREA;
    } else if (pixel[0] == AIRG_WAYPOINT) {
        result = AIRG_WAYPOINT;
    } else if (pixel[0] == PF_SEED_POINT) {
        result = PF_SEED_POINT;
    } else if (pixel[0] == PF_EXCLUSION_BOX) {
        result = PF_EXCLUSION_BOX;
    }
    return {result, result != NONE ? selectedIndex : -1};
}
