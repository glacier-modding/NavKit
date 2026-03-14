#include <iostream>
#include <fstream>
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
#include <GL/glew.h>
#include <SDL.h>

#include "../../include/NavKit/render/Model.h"
#include "../../include/NavKit/module/Logger.h"

#include <ranges>

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

    SDL_Surface* surface = SDL_LoadBMP(filename.c_str());
    if (!surface) {
        if (filename.size() > 4 && (filename.substr(filename.size() - 4) == ".TGA" || filename.substr(
            filename.size() - 4) == ".tga")) {
            Logger::log(NK_DEBUG, "Attempting manual TGA load: %s", filename.c_str());
            std::ifstream file(filename, std::ios::binary);
            if (file.is_open()) {
                unsigned char header[18];
                file.read((char*)header, 18);
                int width = header[12] | (header[13] << 8);
                int height = header[14] | (header[15] << 8);
                int bpp = header[16];
                if ((header[2] == 2 || header[2] == 10) && (bpp == 24 || bpp == 32)) {
                    Logger::log(NK_DEBUG, "TGA header: type=%d, bpp=%d, width=%d, height=%d, descriptor=0x%02x",
                                (int)header[2], bpp, width, height, (int)header[17]);
                    int size = width * height * (bpp / 8);
                    std::vector<unsigned char> data(size);
                    if (header[2] == 2) {
                        file.read((char*)data.data(), size);
                    } else {
                        int i = 0;
                        while (i < size) {
                            unsigned char p;
                            file.read((char*)&p, 1);
                            if (p & 0x80) {
                                int count = (p & 0x7F) + 1;
                                unsigned char pixel[4];
                                file.read((char*)pixel, bpp / 8);
                                for (int j = 0; j < count; ++j) {
                                    for (int k = 0; k < bpp / 8; ++k) {
                                        if (i < size)
                                            data[i++] = pixel[k];
                                    }
                                }
                            } else {
                                int count = p + 1;
                                int bytesToRead = count * (bpp / 8);
                                if (i + bytesToRead <= size) {
                                    file.read((char*)&data[i], bytesToRead);
                                    i += bytesToRead;
                                } else {
                                    file.read((char*)&data[i], size - i);
                                    i = size;
                                }
                            }
                        }
                    }

                    if (!(header[17] & 0x20)) {
                        int rowSize = width * (bpp / 8);
                        std::vector<unsigned char> row(rowSize);
                        for (int y = 0; y < height / 2; ++y) {
                            int topIdx = y * rowSize;
                            int bottomIdx = (height - 1 - y) * rowSize;
                            std::copy(data.begin() + topIdx, data.begin() + topIdx + rowSize, row.begin());
                            std::copy(data.begin() + bottomIdx, data.begin() + bottomIdx + rowSize,
                                      data.begin() + topIdx);
                            std::copy(row.begin(), row.end(), data.begin() + bottomIdx);
                        }
                    }

                    if (bpp == 32) {
                        for (size_t i = 3; i < data.size(); i += 4) {
                            data[i] = 255;
                        }
                    }

                    texture.width = width;
                    texture.height = height;
                    texture.bpp = bpp;
                    texture.data = std::move(data);
                    texture.internalFormat = (bpp == 32) ? GL_RGBA8 : GL_RGB8;
                    texture.uploadFormat = (bpp == 32) ? GL_BGRA : GL_BGR;
                    texture.loaded = true;

                    unsigned long long sum = 0;
                    for (size_t j = 0; j < texture.data.size(); ++j) {
                        sum += texture.data[j];
                    }
                    Logger::log(NK_DEBUG, "TGA data sum: %llu, average: %f, samples: %02x %02x %02x %02x, format: 0x%x",
                                sum, (double)sum / texture.data.size(), texture.data[0], texture.data[1],
                                texture.data[2], (bpp == 32 ? texture.data[3] : 0), texture.uploadFormat);

                    Logger::log(NK_DEBUG, "Manual TGA load successful (deferred): %s", filename.c_str());
                    return texture;
                } else {
                    Logger::log(NK_ERROR, "Unsupported TGA format: type=%d, bpp=%d", (int)header[2], bpp);
                }
            } else {
                Logger::log(NK_ERROR, "Failed to open TGA file: %s", filename.c_str());
            }
        }
    }

    if (surface) {
        Logger::log(NK_DEBUG, "Texture loaded successfully using SDL_LoadBMP: %s", filename.c_str());
        SDL_Surface* formattedSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(surface);

        if (formattedSurface) {
            texture.width = formattedSurface->w;
            texture.height = formattedSurface->h;
            texture.bpp = 32;
            int size = texture.width * texture.height * 4;
            texture.data.assign((unsigned char*)formattedSurface->pixels,
                                (unsigned char*)formattedSurface->pixels + size);
            texture.internalFormat = GL_RGBA8;
            texture.uploadFormat = GL_RGBA;
            texture.loaded = true;
            SDL_FreeSurface(formattedSurface);
        } else {
            Logger::log(NK_ERROR, "Texture failed to convert at path: %s", filename.c_str());
        }
    } else {
        Logger::log(NK_ERROR, "Texture failed to load at path: %s", filename.c_str());
    }

    return texture;
}

std::map<const Model*, SortContext> Model::sortContexts;

Mesh Model::processBatchedMeshes(const std::vector<aiMesh*>& batch, const aiScene* scene, const std::string& directory,
                                 std::vector<Texture>& texturesLoaded) {
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
                if (i == 0) {
                    Logger::log(NK_DEBUG, "Mesh %s has UVs: %f, %f", mesh->mName.C_Str(), vec.x, vec.y);
                }
            } else {
                vertex.texCoords = glm::vec2(0.0f, 0.0f);
                if (i == 0) {
                    Logger::log(NK_DEBUG, "Mesh %s has NO UVs", mesh->mName.C_Str());
                }
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

        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
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
    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
        aiString str;
        mat->GetTexture(type, i, &str);
        bool skip = false;
        for (unsigned int j = 0; j < texturesLoaded.size(); j++) {
            if (std::strcmp(texturesLoaded[j].path.data, str.C_Str()) == 0) {
                textures.push_back(texturesLoaded[j]);
                skip = true;
                break;
            }
        }
        if (!skip) {
            Texture texture = loadTextureDataFromFile(str.C_Str(), directory);
            texture.type = typeName;
            texture.path = str;
            textures.push_back(texture);
            texturesLoaded.push_back(texture);
        }
    }
    return textures;
}

void Model::loadModelData(std::string const& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipWindingOrder | aiProcess_FlipUVs);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return;
    }
    directory = path.substr(0, path.find_last_of('/'));
    if (directory == path) {
        directory = path.substr(0, path.find_last_of('\\'));
    }
    if (directory == path) {
        directory = "";
    }

    texturesLoaded.clear();

    Logger::log(NK_DEBUG, "Loading model: %s, base directory: %s", path.c_str(), directory.c_str());

    meshes.clear();
    texturesLoaded.clear();

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

    for (auto& [key, batch] : batches) {
        meshes.push_back(processBatchedMeshes(batch, scene, directory, texturesLoaded));
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

            Logger::log(NK_DEBUG, "Texture uploaded to GPU (ID:%u): %s", texture.id, texture.path.C_Str());

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
