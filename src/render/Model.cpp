#include <iostream>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <array>
#include <algorithm>
#include <vector>
#include <future>
#include <map>
#include <numeric>

#include "../../include/NavKit/render/Model.h"

#include <ranges>
std::map<const Model*, SortContext> Model::sortContexts;

Mesh Model::processBatchedMeshes(const std::vector<aiMesh*>& batch) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    unsigned int vertexOffset = 0;

    for (aiMesh* mesh : batch) {
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;
            glm::vec3 vector;
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;

            if (mesh->HasNormals()) {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            } else {
                vertex.Normal = glm::vec3(0.0f);
            }

            if (mesh->mTextureCoords[0]) {
                glm::vec2 vec;
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
            } else {
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);
            }
            vertices.push_back(vertex);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j] + vertexOffset);
            }
        }

        vertexOffset += mesh->mNumVertices;
    }

    return Mesh(vertices, indices, textures);
}

void Model::loadModelData(std::string const& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_FlipWindingOrder);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return;
    }
    meshes.clear();

    std::map<std::string, std::vector<aiMesh*>> batches;

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        std::string name = mesh->mName.C_Str();

        std::string batchId = name;
        if (const size_t underscorePos = name.find('_'); underscorePos != std::string::npos) {
            batchId = name.substr(0, underscorePos);
        }

        std::string key = batchId + "_" + std::to_string(mesh->mMaterialIndex);

        batches[key].push_back(mesh);
    }

    for (auto& val : batches | std::views::values) {
        meshes.push_back(processBatchedMeshes(val));
    }
}

void Model::initGl() {
    for (unsigned int i = 0; i < meshes.size(); i++) {
        meshes[i].setupMesh();
    }
}

void Model::draw(const Shader& shader, const glm::mat4& viewProj) const {
    std::array<glm::vec4, 6> planes;
    for (int i = 0; i < 6; ++i) {
        planes[i] = glm::vec4(
            viewProj[0][3] + (i % 2 == 0 ? 1 : -1) * viewProj[0][i / 2],
            viewProj[1][3] + (i % 2 == 0 ? 1 : -1) * viewProj[1][i / 2],
            viewProj[2][3] + (i % 2 == 0 ? 1 : -1) * viewProj[2][i / 2],
            viewProj[3][3] + (i % 2 == 0 ? 1 : -1) * viewProj[3][i / 2]
        );
    }

    auto& [drawOrder, sortFuture] = sortContexts[this];

    if (drawOrder.size() != meshes.size()) {
        drawOrder.resize(meshes.size());
        std::iota(drawOrder.begin(), drawOrder.end(), 0);
    }

    if (sortFuture.valid() && sortFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        if (std::vector<unsigned int> newOrder = sortFuture.get(); newOrder.size() == meshes.size()) {
            drawOrder = std::move(newOrder);
        }
    }

    if (!sortFuture.valid()) {
        std::vector<glm::vec3> centers;
        centers.reserve(meshes.size());
        for (const auto& mesh : meshes) {
            centers.push_back((mesh.aabbMin + mesh.aabbMax) * 0.5f);
        }

        sortFuture = std::async(std::launch::async, [centers = std::move(centers), viewProj]() {
            std::vector<unsigned int> indices(centers.size());
            std::iota(indices.begin(), indices.end(), 0);

            std::vector<float> distances(centers.size());
            for (size_t i = 0; i < centers.size(); ++i) {
                distances[i] = (viewProj * glm::vec4(centers[i], 1.0f)).w;
            }

            std::ranges::sort(indices, [&](unsigned int a, unsigned int b) {
                return distances[a] < distances[b];
            });
            return indices;
        });
    }

    for (const unsigned int i : drawOrder) {
        const Mesh& mesh = meshes[i];
        const glm::vec3 center = (mesh.aabbMin + mesh.aabbMax) * 0.5f;
        const glm::vec3 extents = (mesh.aabbMax - mesh.aabbMin) * 0.5f;
        bool inside = true;
        for (const auto& plane : planes) {
            const float r = extents.x * std::abs(plane.x) + extents.y * std::abs(plane.y) + extents.z *
                std::abs(plane.z);
            if (const float d = glm::dot(glm::vec3(plane), center) + plane.w; d < -r) {
                inside = false;
                break;
            }
        }
        if (inside) {
            mesh.draw(shader);
        }
    }
}
