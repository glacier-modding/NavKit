#include <string>
#include <vector>
#include <assimp/types.h>
#include <GL/glew.h>
#include <glm/glm.hpp>

#include "../../include/NavKit/render/Mesh.h"
#include "../../include/NavKit/module/Logger.h"

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices,
           const std::vector<Texture>& textures) {
    this->vertices = vertices;
    this->indices = indices;
    this->textures = textures;
    this->VAO = 0;
    this->VBO = 0;
    this->EBO = 0;
    this->aabbMin = glm::vec3(0.0f);
    this->aabbMax = glm::vec3(0.0f);

    if (!vertices.empty()) {
        aabbMin = vertices[0].Position;
        aabbMax = vertices[0].Position;
        for (const auto& v : vertices) {
            aabbMin = glm::min(aabbMin, v.Position);
            aabbMax = glm::max(aabbMax, v.Position);
        }
    }
}

void Mesh::draw(Shader& shader) const {
    // Bind appropriate textures
    // ... (binding logic for diffuse/specular/normal maps) ...
    shader.use();
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::setupMesh() {
    if (VAO != 0) {
        Logger::log(NK_INFO, "Mesh::setupMesh: VAO is not null");
        return;
    }

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0); // positions
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal)); // normals
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
    // texture coords

    glBindVertexArray(0);
}
