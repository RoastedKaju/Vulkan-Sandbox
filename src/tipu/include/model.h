#pragma once

#include <filesystem>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <assimp/material.h>

#include "assimp/scene.h"
#include "image.h"

class Context;

struct aiScene;
struct aiNode;
struct aiMesh;

struct Vertex {
    glm::vec3 position_;
    glm::vec3 normal_;
    glm::vec2 uv_;
    glm::vec4 tangent_; // w: bitangent
};

struct MeshData {
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
};

enum class TextureType {
    Diffuse,
    Specular,
    Normal,
    Height
};

struct Texture {
    std::shared_ptr<Image> image_;
    TextureType type_ = TextureType::Diffuse;
    std::filesystem::path path_;
};

/**
 * A single drawable collection of vertices and indcies
 */
class Mesh {
public:
    Mesh() = default;

    Mesh(MeshData data, std::vector<Texture> textures)
        : data_(std::move(data)), textures_(std::move(textures)) {
    }

    MeshData &data() { return data_; }
    const MeshData &data() const { return data_; }

    std::vector<Texture> &textures() { return textures_; }
    const std::vector<Texture> &textures() const { return textures_; }

private:
    MeshData data_;
    std::vector<Texture> textures_;
};

/***
 * Container class for meshes, interface to load models
 */
class Model {
public:
    Model() = default;

    bool load(Context *context, const std::filesystem::path &path);

    std::vector<Mesh> &meshes() { return meshes_; }
    const std::vector<Mesh> &meshes() const { return meshes_; }

    const std::filesystem::path &directory() const { return directory_; }

private:
    void process_node(const aiNode *node, const aiScene *scene);

    Mesh process_mesh(const aiMesh *mesh, const aiScene *scene);

    std::vector<Texture> load_material_textures(const aiMaterial *material, aiTextureType ai_type, TextureType type);

    std::vector<Mesh> meshes_;
    std::filesystem::path directory_;

    std::unordered_map<std::string, Texture> loaded_textures_;

    Context *context_;
};
