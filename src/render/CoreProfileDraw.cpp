#include "../../include/NavKit/render/CoreProfileDraw.h"

#include "../../include/NavKit/module/NavKitSettings.h"
#include "../../include/NavKit/module/Renderer.h"
#include <utility>

namespace {
    bool g_fogEnabled = true;
}

namespace {
    void configureCoreProfileVertexAttributes() {
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CoreProfileVertex),
            reinterpret_cast<void*>(offsetof(CoreProfileVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CoreProfileVertex),
            reinterpret_cast<void*>(offsetof(CoreProfileVertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(CoreProfileVertex),
            reinterpret_cast<void*>(offsetof(CoreProfileVertex, color)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(CoreProfileVertex),
            reinterpret_cast<void*>(offsetof(CoreProfileVertex, texCoords)));
    }

    void beginCoreProfileDraw(const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection,
        const GLuint texture, const bool alphaTexture, const bool fog) {
        Renderer& renderer = Renderer::getInstance();
        Shader& shader = renderer.shader;
        shader.use();
        shader.setMat4("model", model);
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        shader.setMat3("normalMatrix", glm::mat3(1.0f));
        shader.setVec4("flatColor", glm::vec4(1.0f));
        shader.setBool("useFlatColor", true);
        shader.setBool("useVertexColor", true);
        shader.setBool("useTexture", false);
        shader.setBool("useUiTexture", texture != 0);
        shader.setBool("useAlphaTexture", alphaTexture);
        shader.setBool("useFog", fog && g_fogEnabled);
        shader.setInt("uiTexture", 0);
        shader.setVec4("fogColor",
            glm::vec4(NavKitSettings::getInstance().backgroundColor, NavKitSettings::getInstance().backgroundColor,
                NavKitSettings::getInstance().backgroundColor, 1.0f));
        shader.setFloat("fogStart", renderer.camr * 0.1f);
        shader.setFloat("fogEnd", renderer.camr * 1.25f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    void endCoreProfileDraw() {
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        Shader& shader = Renderer::getInstance().shader;
        shader.setBool("useUiTexture", false);
        shader.setBool("useAlphaTexture", false);
        glUseProgram(0);
    }
} // namespace

void drawCoreProfileVertices(const std::span<const CoreProfileVertex> vertices, const GLenum primitive,
    const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection, const GLuint texture,
    const bool alphaTexture, const bool fog) {
    if (vertices.empty()) {
        return;
    }

    static GLuint vao = 0;
    static GLuint vbo = 0;
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        configureCoreProfileVertexAttributes();
        glBindVertexArray(0);
    }

    beginCoreProfileDraw(model, view, projection, texture, alphaTexture, fog);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size_bytes(), vertices.data(), GL_STREAM_DRAW);
    glDrawArrays(primitive, 0, static_cast<GLsizei>(vertices.size()));
    endCoreProfileDraw();
}

CoreProfileVertexBuffer::CoreProfileVertexBuffer(CoreProfileVertexBuffer&& other) noexcept :
    vao(std::exchange(other.vao, 0)), vbo(std::exchange(other.vbo, 0)),
    vertexCount(std::exchange(other.vertexCount, 0)) {}

CoreProfileVertexBuffer& CoreProfileVertexBuffer::operator=(CoreProfileVertexBuffer&& other) noexcept {
    if (this != &other) {
        release();
        vao = std::exchange(other.vao, 0);
        vbo = std::exchange(other.vbo, 0);
        vertexCount = std::exchange(other.vertexCount, 0);
    }
    return *this;
}

void CoreProfileVertexBuffer::release() {
    if (vao != 0) {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
    }
    vao = 0;
    vbo = 0;
    vertexCount = 0;
}

void CoreProfileVertexBuffer::upload(const std::span<const CoreProfileVertex> vertices) {
    if (vao == 0) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        configureCoreProfileVertexAttributes();
    } else {
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
    }
    glBufferData(GL_ARRAY_BUFFER, vertices.size_bytes(), vertices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    vertexCount = static_cast<GLsizei>(vertices.size());
}

void CoreProfileVertexBuffer::draw(
    const GLenum primitive, const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection) const {
    if (vertexCount == 0) {
        return;
    }
    beginCoreProfileDraw(model, view, projection, 0, false, true);
    glBindVertexArray(vao);
    glDrawArrays(primitive, 0, vertexCount);
    endCoreProfileDraw();
}

void setCoreProfileFogEnabled(const bool enabled) {
    g_fogEnabled = enabled;
}
