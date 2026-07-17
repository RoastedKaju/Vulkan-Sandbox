#pragma once

#include <filesystem>
#include <utility>
#include <vector>
#include <unordered_map>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <assimp/material.h>

#include "assimp/scene.h"
#include "image.h"

static constexpr int kMaxBoneInfulence = 4;

class Context;

struct aiScene;
struct aiNode;
struct aiMesh;

struct Vertex {
    glm::vec3 position_;
    glm::vec3 normal_;
    glm::vec2 uv_;
    glm::vec4 tangent_; // w: bitangent
    glm::ivec4 bone_ids_{-1, -1, -1, -1};
    glm::vec4 bone_weights_{0.0f};
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

struct BoneInfo {
    int id_;
    glm::mat4 offset_; // inverse bind pose, mesh space -> bone space
};

struct SkeletonNode {
    std::string name_;
    glm::mat4 local_transform_;
    int parent_ = -1;
    std::vector<int> children_;
};

struct Skeleton {
    std::unordered_map<std::string, BoneInfo> bone_info_map_;
    std::vector<SkeletonNode> nodes_;
    int root_ = -1;
    glm::mat4 global_inverse_transform_{1.0f};
};

struct KeyPosition {
    double time_;
    glm::vec3 value_;
};

struct KeyRotation {
    double time_;
    glm::quat value_;
};

struct KeyScale {
    double time_;
    glm::vec3 value_;
};

struct NodeAnimation {
    std::vector<KeyPosition> positions_;
    std::vector<KeyRotation> rotations_;
    std::vector<KeyScale> scales_;
};

struct AnimationClip {
    std::string name_;
    double duration_;
    double ticks_per_second_;
    std::unordered_map<std::string, NodeAnimation> channels_; // keyed by node name
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

    bool load(Context *context, const std::filesystem::path &path, bool is_skeletal_mesh = false);

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

    // get skeleton
    const Skeleton &skeleton() const { return skeleton_; }
    const std::vector<AnimationClip> &animations() const { return animations_; }
    bool is_skeletal_mesh() const { return is_skeletal_mesh_; }

private:
    void process_node(const aiNode *node, const aiScene *scene);

    Mesh process_mesh(const aiMesh *mesh, const aiScene *scene);

    Material process_material(const aiScene *scene, const aiMaterial *ai_material);

    std::shared_ptr<Texture> load_material_texture(const aiScene *scene,
                                                   const aiMaterial *material,
                                                   aiTextureType ai_type,
                                                   unsigned int index = 0);

    int process_skeleton_node(const aiNode *node, int parent);

    std::vector<Mesh> meshes_;
    std::vector<Material> materials_;
    std::filesystem::path directory_;

    bool is_skeletal_mesh_ = false;
    Skeleton skeleton_;
    std::vector<AnimationClip> animations_;

    std::unordered_map<std::string, std::shared_ptr<Texture> > texture_cache_;

    Context *context_;
};
