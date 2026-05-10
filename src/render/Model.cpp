#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <array>
#include <algorithm>
#include <vector>
#include <future>
#include <map>
#include <GL/glew.h>
#include <SDL.h>
#include <mutex>

#include <stb_image.h>
#include "../../include/NavKit/render/Model.h"
#include "../../include/NavKit/module/Logger.h"

#include <ranges>

static std::mutex g_TextureMutex;

Texture loadTextureDataFromFile(const char* path, const std::string& directory) {
    Texture texture;
    texture.id = 0;
    texture.loaded = false;

    std::string filename = std::string(path);

    std::replace(filename.begin(), filename.end(), '/', '\\');

    bool isAbsolute = (filename.size() >= 2 && filename[1] == ':') || (filename.size() >= 1 && filename[0] == '\\');

    if (!isAbsolute && !directory.empty()) {
        std::string dir = directory;
        std::replace(dir.begin(), dir.end(), '/', '\\');
        if (dir.back() != '\\') {
            dir += '\\';
        }
        filename = dir + filename;
    }

    int width, height, nrChannels;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &nrChannels, 0);

    if (data) {
        texture.width = width;
        texture.height = height;
        texture.bpp = nrChannels * 8;
        int size = width * height * nrChannels;
        texture.data.assign(data, data + size);

        if (nrChannels == 3) {
            texture.internalFormat = GL_RGB8;
            texture.uploadFormat = GL_RGB;
        } else if (nrChannels == 4) {
            texture.internalFormat = GL_RGBA8;
            texture.uploadFormat = GL_RGBA;
        }

        texture.loaded = true;
        stbi_image_free(data);
    } else {
        Logger::log(NK_ERROR, "Texture failed to load at path: %s. Reason: %s", filename.c_str(),
                    stbi_failure_reason());
    }

    return texture;
}

std::map<const Model*, SortContext> Model::sortContexts;

Mesh Model::processBatchedMeshes(const std::vector<aiMesh*>& batch, const aiScene* scene, const std::string& directory,
                                 std::vector<Texture>& texturesLoaded) {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    unsigned int totalVertices = 0;
    unsigned int totalIndices = 0;
    for (aiMesh* mesh : batch) {
        totalVertices += mesh->mNumVertices;
        totalIndices += mesh->mNumFaces * 3;
    }
    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    unsigned int vertexOffset = 0;
    for (aiMesh* mesh : batch) {
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex vertex;
            glm::vec3 vector;
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.position = vector;

            if (mesh->HasNormals()) {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.normal = vector;
            } else {
                vertex.normal = glm::vec3(0.0f);
            }

            if (mesh->mTextureCoords[0]) {
                glm::vec2 vec;
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.texCoords = vec;
            } else {
                vertex.texCoords = glm::vec2(0.0f, 0.0f);
            }

            if (mesh->HasVertexColors(0)) {
                vertex.color.r = mesh->mColors[0][i].r;
                vertex.color.g = mesh->mColors[0][i].g;
                vertex.color.b = mesh->mColors[0][i].b;
                vertex.color.a = mesh->mColors[0][i].a;
            } else {
                vertex.color = glm::vec4(1.0f);
            }

            vertices.push_back(vertex);
        }

        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                indices.push_back(face.mIndices[j] + vertexOffset);
            }
        }

        vertexOffset += mesh->mNumVertices;
    }

    if (!batch.empty() && scene) {
        aiMaterial* material = scene->mMaterials[batch[0]->mMaterialIndex];
        std::vector<Texture> diffuseMaps = loadMaterialTexturesStatic(material, aiTextureType_DIFFUSE,
                                                                      "texture_diffuse", directory, texturesLoaded);
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        std::vector<Texture> specularMaps = loadMaterialTexturesStatic(material, aiTextureType_SPECULAR,
                                                                       "texture_specular", directory, texturesLoaded);
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        std::vector<Texture> normalMaps = loadMaterialTexturesStatic(material, aiTextureType_HEIGHT, "texture_normal",
                                                                     directory, texturesLoaded);
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        std::vector<Texture> heightMaps = loadMaterialTexturesStatic(material, aiTextureType_AMBIENT, "texture_height",
                                                                     directory, texturesLoaded);
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());
    }

    return Mesh(vertices, indices, textures);
}

std::vector<Texture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName) {
    return loadMaterialTexturesStatic(mat, type, typeName, directory, texturesLoaded);
}

std::vector<Texture> Model::loadMaterialTexturesStatic(aiMaterial* mat, aiTextureType type, std::string typeName,
                                                       const std::string& directory,
                                                       std::vector<Texture>& texturesLoaded) {
    std::vector<Texture> textures;
    for (unsigned int i = 0; i < mat->GetTextureCount(type); ++i) {
        aiString str;
        mat->GetTexture(type, i, &str);
        bool skip = false;
        {
            std::lock_guard lock(g_TextureMutex);
            for (unsigned int j = 0; j < texturesLoaded.size(); ++j) {
                if (std::strcmp(texturesLoaded[j].path.data, str.C_Str()) == 0) {
                    textures.push_back(texturesLoaded[j]);
                    skip = true;
                    break;
                }
            }
        }

        if (!skip) {
            Texture texture = loadTextureDataFromFile(str.C_Str(), directory);
            texture.type = typeName;
            texture.path = str;
            textures.push_back(texture);
            {
                std::lock_guard lock(g_TextureMutex);
                texturesLoaded.push_back(texture);
            }
        }
    }
    return textures;
}

void Model::loadModelData(std::string const& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipWindingOrder | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        Logger::log(NK_ERROR, "ERROR::ASSIMP::%s", importer.GetErrorString());
        return;
    }
    directory = path.substr(0, path.find_last_of('/'));
    if (directory == path) {
        directory = path.substr(0, path.find_last_of('\\'));
    }
    if (directory == path) {
        directory = "";
    }

    meshes.clear();
    texturesLoaded.clear();

    std::map<std::pair<std::string, unsigned int>, std::vector<aiMesh*>> batches;

    for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        std::string name = mesh->mName.C_Str();

        std::string batchId = name;
        if (const size_t underscorePos = name.find('_'); underscorePos != std::string::npos) {
            batchId = name.substr(0, underscorePos);
        }

        batches[{batchId, mesh->mMaterialIndex}].push_back(mesh);
    }

    std::vector<std::future<Mesh>> meshFutures;
    for (auto& batch : batches | std::views::values) {
        meshFutures.push_back(std::async(std::launch::async, [this, &batch, scene]() {
            return processBatchedMeshes(batch, scene, this->directory, this->texturesLoaded);
        }));
    }

    for (auto& fut : meshFutures) {
        meshes.push_back(fut.get());
    }
}

void Model::initGl() {
    for (auto& texture : texturesLoaded) {
        if (texture.loaded && texture.id == 0) {
            glGenTextures(1, &texture.id);
            glBindTexture(GL_TEXTURE_2D, texture.id);

            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glTexImage2D(GL_TEXTURE_2D, 0, texture.internalFormat, texture.width, texture.height, 0,
                         texture.uploadFormat, GL_UNSIGNED_BYTE, texture.data.data());
            glGenerateMipmap(GL_TEXTURE_2D);

            texture.data.clear();
            texture.data.shrink_to_fit();
        }
    }

    for (auto& mesh : meshes) {
        for (auto& meshTexture : mesh.textures) {
            if (meshTexture.id == 0) {
                for (const auto& loadedTex : texturesLoaded) {
                    if (std::strcmp(meshTexture.path.data, loadedTex.path.data) == 0) {
                        meshTexture.id = loadedTex.id;
                        break;
                    }
                }
            }
        }
        mesh.setupMesh();
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
        const float length = glm::length(glm::vec3(planes[i]));
        if (length > 0.0f) {
            planes[i] /= length;
        }
    }

    std::vector<unsigned int> opaqueIndices;
    std::vector<unsigned int> transparentIndices;
    opaqueIndices.reserve(meshes.size());
    transparentIndices.reserve(meshes.size());

    for (unsigned int i = 0; i < meshes.size(); ++i) {
        if (meshes[i].isBlended) {
            transparentIndices.push_back(i);
        } else {
            opaqueIndices.push_back(i);
        }
    }

    std::vector<float> distances(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i) {
        const glm::vec3 center = (meshes[i].aabbMin + meshes[i].aabbMax) * 0.5f;
        distances[i] = (viewProj * glm::vec4(center, 1.0f)).w;
    }

    // Sort Opaque Front-to-Back (Optimizes overdraw)
    std::sort(opaqueIndices.begin(), opaqueIndices.end(), [&](unsigned int a, unsigned int b) {
        return distances[a] < distances[b];
    });

    // Sort Transparent Back-to-Front (Required for correct blending)
    std::sort(transparentIndices.begin(), transparentIndices.end(), [&](unsigned int a, unsigned int b) {
        return distances[a] > distances[b];
    });

    // Pass 1: Opaque meshes (including cutout transparency)
    for (const unsigned int i : opaqueIndices) {
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

    // Pass 2: Blended transparent meshes (sorted back-to-front by distances[a] > distances[b])
    for (const unsigned int i : transparentIndices) {
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
