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
