#pragma once
#include <string>
#include <vector>
#include <assimp/types.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "Shader.h"

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
};

struct Texture {
    unsigned int id;
    std::string type;
    aiString path;
};
class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    unsigned int VAO, VBO, EBO;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;

    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices, const std::vector<Texture>& textures);

    void draw(Shader& shader) const;

    void setupMesh();
};
