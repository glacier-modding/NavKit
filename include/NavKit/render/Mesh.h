#pragma once
#include <string>
#include <vector>
#include <assimp/types.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "Shader.h"

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec4 color;
    glm::vec2 texCoords;
};

struct Texture {
    unsigned int id;
    std::string type;
    aiString path;
    std::vector<unsigned char> data;
    int width = 0;
    int height = 0;
    int bpp = 0;
    unsigned int internalFormat = 0;
    unsigned int uploadFormat = 0;
    bool loaded = false;
};

class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;
    unsigned int VAO, VBO, EBO;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;

    bool isTransparent = false;
    bool isBlended = false;

    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices,
         const std::vector<Texture>& textures);

    void draw(const Shader& shader) const;

    void setupMesh();
};
