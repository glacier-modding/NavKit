#pragma once

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <span>

struct CoreProfileVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 color;
    glm::vec2 texCoords;
};

void drawCoreProfileVertices(std::span<const CoreProfileVertex> vertices, GLenum primitive, const glm::mat4& model,
    const glm::mat4& view, const glm::mat4& projection, GLuint texture = 0, bool alphaTexture = false, bool fog = true);
void setCoreProfileFogEnabled(bool enabled);

class CoreProfileVertexBuffer {
public:
    CoreProfileVertexBuffer() = default;
    CoreProfileVertexBuffer(const CoreProfileVertexBuffer&) = delete;
    CoreProfileVertexBuffer& operator=(const CoreProfileVertexBuffer&) = delete;
    CoreProfileVertexBuffer(CoreProfileVertexBuffer&& other) noexcept;
    CoreProfileVertexBuffer& operator=(CoreProfileVertexBuffer&& other) noexcept;

    // Not done in a destructor: it needs a current GL context, which teardown and tests lack.
    void release();
    void upload(std::span<const CoreProfileVertex> vertices);
    void draw(GLenum primitive, const glm::mat4& model, const glm::mat4& view, const glm::mat4& projection) const;

private:
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei vertexCount = 0;
};
