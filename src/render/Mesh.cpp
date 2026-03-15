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
        aabbMin = vertices[0].position;
        aabbMax = vertices[0].position;
        for (const auto& v : vertices) {
            aabbMin = glm::min(aabbMin, v.position);
            aabbMax = glm::max(aabbMax, v.position);
            if (v.color.a < 0.99f) {
                isTransparent = true;
            }
        }
    }

    for (const auto& t : textures) {
        if (t.uploadFormat == GL_RGBA) {
            isTransparent = true;
            break;
        }
    }
}

void Mesh::draw(const Shader& shader) const {
    unsigned int diffuseNr = 1;
    unsigned int specularNr = 1;
    unsigned int normalNr = 1;
    unsigned int heightNr = 1;
    for (unsigned int i = 0; i < textures.size(); i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        std::string number;
        std::string name = textures[i].type;
        if (name == "texture_diffuse")
            number = std::to_string(diffuseNr++);
        else if (name == "texture_specular")
            number = std::to_string(specularNr++);
        else if (name == "texture_normal")
            number = std::to_string(normalNr++);
        else if (name == "texture_height")
            number = std::to_string(heightNr++);

        shader.setInt(name + number, static_cast<int>(i));
        glBindTexture(GL_TEXTURE_2D, textures[i].id);

        static int logCounterBind = 0;
        if (logCounterBind < 10) {
            Logger::log(NK_DEBUG, "Mesh::draw bind: unit=%d, name=%s, id=%u, valid=%d", i, (name + number).c_str(),
                        textures[i].id, glIsTexture(textures[i].id));
            logCounterBind++;
        }
    }

    shader.setBool("useTexture", !textures.empty());
    if (!textures.empty()) {
        static int logCounterTex = 0;
        if (logCounterTex < 10) {
            Logger::log(NK_DEBUG, "Mesh::draw: using textures, count: %zu, useTexture: 1, useFlatColor: 0",
                        textures.size());
            logCounterTex++;
        }
        shader.setBool("useFlatColor", false);
    } else {
        static int logCounterNoTex = 0;
        if (logCounterNoTex < 10) {
            Logger::log(NK_DEBUG, "Mesh::draw: no textures, useTexture: 0");
            logCounterNoTex++;
        }
    }

    shader.use();
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glActiveTexture(GL_TEXTURE0);
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), static_cast<void*>(nullptr)); // positions
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal))); // normals
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, texCoords)));
    // texture coords

    glBindVertexArray(0);
}
