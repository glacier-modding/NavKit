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
    std::vector<Texture> texturesLoaded;
    std::string directory;

    static Mesh processBatchedMeshes(const std::vector<aiMesh*>& batch, const aiScene* scene, const std::string& directory, std::vector<Texture>& texturesLoaded);

    void loadModelData(std::string const& path);

    void draw(const Shader& shader, const glm::mat4& viewProj) const;

private:
    std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName);

    static std::vector<Texture> loadMaterialTexturesStatic(aiMaterial* mat, aiTextureType type, std::string typeName, const std::string& directory, std::vector<Texture>& texturesLoaded);
};
