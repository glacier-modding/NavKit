#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <array>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <future>
#include <limits>
#include <map>
#include <GL/glew.h>
#include <mutex>

#include <stb_image.h>
#include "../../include/NavKit/render/Model.h"
#include "../../include/NavKit/module/Logger.h"

#include <ranges>

static std::mutex g_TextureMutex;

static bool hasBlendedAlpha(const std::vector<unsigned char>& data) {
    for (size_t alphaIndex = 3; alphaIndex < data.size(); alphaIndex += 4) {
        if (data[alphaIndex] > 25 && data[alphaIndex] < 255) {
            return true;
        }
    }
    return false;
}

Texture loadTextureDataFromFile(const char* path, const std::string& directory) {
    Texture texture;
    texture.id = 0;
    texture.loaded = false;

    std::string normalizedPath(path);
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
    std::filesystem::path texturePath = std::filesystem::u8path(normalizedPath);
    if (texturePath.is_relative() && !directory.empty()) {
        texturePath = std::filesystem::u8path(directory) / texturePath;
    }
    const std::string filename = texturePath.string();

    int width, height, nrChannels;
    unsigned char* data = stbi_load(filename.c_str(), &width, &height, &nrChannels, STBI_rgb_alpha);

    if (data) {
        texture.width = width;
        texture.height = height;
        texture.bpp = 32;
        const int size = width * height * 4;
        texture.data.assign(data, data + size);
        texture.internalFormat = GL_RGBA8;
        texture.uploadFormat = GL_RGBA;
        texture.hasBlendedAlpha = hasBlendedAlpha(texture.data);

        texture.loaded = true;
        stbi_image_free(data);
    } else {
        Logger::log(
            NK_ERROR, "Texture failed to load at path: %s. Reason: %s", filename.c_str(), stbi_failure_reason());
    }

    return texture;
}

static Texture loadTextureDataFromEmbedded(const aiTexture& embedded, const aiString& path) {
    Texture texture;
    texture.id = 0;
    texture.loaded = false;
    texture.path = path;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = nullptr;
    if (embedded.mHeight == 0 && embedded.mWidth <= static_cast<unsigned int>(std::numeric_limits<int>::max())) {
        data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(embedded.pcData),
            static_cast<int>(embedded.mWidth), &width, &height, &channels, STBI_rgb_alpha);
    } else if (embedded.mHeight > 0) {
        width = static_cast<int>(embedded.mWidth);
        height = static_cast<int>(embedded.mHeight);
        texture.data.resize(static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < static_cast<size_t>(width) * height; ++i) {
            texture.data[i * 4] = embedded.pcData[i].r;
            texture.data[i * 4 + 1] = embedded.pcData[i].g;
            texture.data[i * 4 + 2] = embedded.pcData[i].b;
            texture.data[i * 4 + 3] = embedded.pcData[i].a;
        }
        texture.width = width;
        texture.height = height;
        texture.bpp = 32;
        texture.internalFormat = GL_RGBA8;
        texture.uploadFormat = GL_RGBA;
        texture.hasBlendedAlpha = hasBlendedAlpha(texture.data);
        texture.loaded = true;
        return texture;
    }
    if (data) {
        texture.width = width;
        texture.height = height;
        texture.bpp = 32;
        texture.data.assign(data, data + static_cast<size_t>(width) * height * 4);
        texture.internalFormat = GL_RGBA8;
        texture.uploadFormat = GL_RGBA;
        texture.hasBlendedAlpha = hasBlendedAlpha(texture.data);
        texture.loaded = true;
        stbi_image_free(data);
    } else {
        Logger::log(NK_ERROR, "Embedded texture failed to load: %s. Reason: %s", path.C_Str(), stbi_failure_reason());
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
        const aiTextureType diffuseType =
            material->GetTextureCount(aiTextureType_BASE_COLOR) > 0 ? aiTextureType_BASE_COLOR : aiTextureType_DIFFUSE;
        std::vector<Texture> diffuseMaps =
            loadMaterialTexturesStatic(material, diffuseType, "texture_diffuse", scene, directory, texturesLoaded);
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        std::vector<Texture> specularMaps = loadMaterialTexturesStatic(
            material, aiTextureType_SPECULAR, "texture_specular", scene, directory, texturesLoaded);
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        const aiTextureType normalType =
            material->GetTextureCount(aiTextureType_NORMALS) > 0 ? aiTextureType_NORMALS : aiTextureType_HEIGHT;
        std::vector<Texture> normalMaps =
            loadMaterialTexturesStatic(material, normalType, "texture_normal", scene, directory, texturesLoaded);
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        std::vector<Texture> heightMaps = loadMaterialTexturesStatic(
            material, aiTextureType_AMBIENT, "texture_height", scene, directory, texturesLoaded);
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());
    }

    return Mesh(vertices, indices, textures);
}

std::vector<Texture> Model::loadMaterialTexturesStatic(aiMaterial* mat, aiTextureType type, std::string typeName,
    const aiScene* scene, const std::string& directory, std::vector<Texture>& texturesLoaded) {
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
            const aiTexture* embedded = scene ? scene->GetEmbeddedTexture(str.C_Str()) : nullptr;
            Texture texture = embedded ? loadTextureDataFromEmbedded(*embedded, str)
                                       : loadTextureDataFromFile(str.C_Str(), directory);
            texture.type = typeName;
            texture.path = str;
            textures.push_back(texture);
            {
                std::lock_guard lock(g_TextureMutex);
                texturesLoaded.push_back(texture);
            }
        } else {
            textures.back().type = typeName;
        }
    }
    return textures;
}

void Model::loadModelData(std::string const& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path,
        aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipWindingOrder | aiProcess_FlipUVs |
            aiProcess_PreTransformVertices);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        Logger::log(NK_ERROR, "ERROR::ASSIMP::%s", importer.GetErrorString());
        return;
    }
    directory = std::filesystem::u8path(path).parent_path().string();

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
    for (const auto& batch : batches | std::views::values) {
        meshFutures.push_back(std::async(std::launch::async, [this, batch, scene]() {
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
        planes[i] = glm::vec4(viewProj[0][3] + (i % 2 == 0 ? 1 : -1) * viewProj[0][i / 2],
            viewProj[1][3] + (i % 2 == 0 ? 1 : -1) * viewProj[1][i / 2],
            viewProj[2][3] + (i % 2 == 0 ? 1 : -1) * viewProj[2][i / 2],
            viewProj[3][3] + (i % 2 == 0 ? 1 : -1) * viewProj[3][i / 2]);
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
    std::sort(opaqueIndices.begin(), opaqueIndices.end(),
        [&](unsigned int a, unsigned int b) { return distances[a] < distances[b]; });

    // Sort Transparent Back-to-Front (Required for correct blending)
    std::sort(transparentIndices.begin(), transparentIndices.end(),
        [&](unsigned int a, unsigned int b) { return distances[a] > distances[b]; });

    // Pass 1: Opaque meshes (including cutout transparency)
    for (const unsigned int i : opaqueIndices) {
        const Mesh& mesh = meshes[i];

        const glm::vec3 center = (mesh.aabbMin + mesh.aabbMax) * 0.5f;
        const glm::vec3 extents = (mesh.aabbMax - mesh.aabbMin) * 0.5f;
        bool inside = true;
        for (const auto& plane : planes) {
            const float r =
                extents.x * std::abs(plane.x) + extents.y * std::abs(plane.y) + extents.z * std::abs(plane.z);
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
            const float r =
                extents.x * std::abs(plane.x) + extents.y * std::abs(plane.y) + extents.z * std::abs(plane.z);
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
