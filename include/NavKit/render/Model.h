#pragma once
#include "Mesh.h"
#include <assimp/scene.h>

class Model {
public:
    std::vector<Mesh> meshes;

    void loadModel(std::string const& path);

    void draw(Shader& shader) const;

private:
    void processNode(const aiNode* node, const aiScene* scene);

    static Mesh processMesh(aiMesh* mesh, const aiScene* scene);
};
