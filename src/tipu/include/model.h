#pragma once

#include <filesystem>
#include <utility>
#include <vector>
#include <unordered_map>
#include <utility>
#include <memory>

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

struct Texture {
    std::shared_ptr<Image> image_;
    std::filesystem::path path_;
};

struct Material {
    std::shared_ptr<Texture> base_color_;
    std::shared_ptr<Texture> normal_;
    std::shared_ptr<Texture> metallic_roughness_;
    std::shared_ptr<Texture> occlusion_;
    std::shared_ptr<Texture> emissive_;

    glm::vec4 base_color_factor_{1.0f};
    float metallic_factor_ = 1.0f;
    float roughness_factor_ = 1.0f;
    float emissive_strength_ = 1.0f;
};

/**
 * A single drawable collection of vertices and indcies
 */
class Mesh {
public:
    Mesh() = default;

    Mesh(MeshData data, Material *material)
        : data_(std::move(data)), material_(material) {
    }

    MeshData &data() { return data_; }
    const MeshData &data() const { return data_; }

    Material *material() const { return material_; }

private:
    MeshData data_;
    Material *material_;
};

/***
 * Container class for meshes, interface to load models
 */
class Model {
public:
    Model() = default;

    bool load(Context *context, const std::filesystem::path &path);

    // get meshes
    std::vector<Mesh> &meshes() { return meshes_; }
    const std::vector<Mesh> &meshes() const { return meshes_; }
    // get materials
    std::vector<Material> &materials() { return materials_; }
    const std::vector<Material> &materials() const { return materials_; }
    // get directory
    const std::filesystem::path &directory() const { return directory_; }

    static VkFormat determine_format(aiTextureType ai_type);

    void destroy_textures();

private:
    void process_node(const aiNode *node, const aiScene *scene);

    Mesh process_mesh(const aiMesh *mesh, const aiScene *scene);

    Material process_material(const aiScene *scene, const aiMaterial *ai_material);

    std::shared_ptr<Texture> load_material_texture(const aiScene *scene,
                                                   const aiMaterial *material,
                                                   aiTextureType ai_type,
                                                   unsigned int index = 0);

    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    std::filesystem::path directory_;

    std::unordered_map<std::string, std::shared_ptr<Texture> > texture_cache_;

    Context *context_;
};
