#pragma once
#include "Mesh.h"
#include <future>
#include <assimp/scene.h>

struct SortContext {
    std::vector<unsigned int> drawOrder;
    std::future<std::vector<unsigned int>> sortFuture;
};

class Model {
public:
    std::vector<Mesh> meshes;

    void initGl();

    static std::map<const Model*, SortContext> sortContexts;

    static Mesh processBatchedMeshes(const std::vector<aiMesh*>& batch);

    void loadModelData(std::string const& path);

    void draw(const Shader& shader, const glm::mat4& viewProj) const;
};
