#include "model.h"

#include <format>
#include <iostream>
#include <ranges>

#include <volk.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "utils.h"
#include "importer.h"
#include "context.h"

bool Model::load(Context *context, const std::filesystem::path &path) {
    context_ = context;
    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality
    );

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        const auto msg = std::format("Assimp failed to load {}", path.string());
        std::cout << msg << std::endl;
        return false;
    }

    directory_ = path.parent_path();
    meshes_.clear();
    texture_cache_.clear();

    // reserve materials first as mesh keeps raw pointers to materials so relocation will cause invalid pointers
    materials_.clear();
    materials_.reserve(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        materials_.push_back(process_material(scene, scene->mMaterials[i]));
    }

    process_node(scene->mRootNode, scene);
    return true;
}

VkFormat Model::determine_format(const aiTextureType ai_type) {
    switch (ai_type) {
        case aiTextureType_DIFFUSE:
        case aiTextureType_BASE_COLOR:
        case aiTextureType_EMISSIVE:
        case aiTextureType_EMISSION_COLOR:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case aiTextureType_NORMALS:
        case aiTextureType_HEIGHT:
        case aiTextureType_METALNESS:
        case aiTextureType_DIFFUSE_ROUGHNESS:
        case aiTextureType_AMBIENT_OCCLUSION:
        case aiTextureType_LIGHTMAP:
        case aiTextureType_UNKNOWN:
        default:
            return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

void Model::destroy_textures() {
    for (const auto &texture: texture_cache_ | std::views::values) {
        if (!texture || !texture->image_) {
            continue;
        }
        if (texture->image_->view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(context_->get_device(), texture->image_->view_, nullptr);
        }
        if (texture->image_->image_ != VK_NULL_HANDLE && texture->image_->allocation_ != VK_NULL_HANDLE) {
            vmaDestroyImage(context_->get_allocator(), texture->image_->image_, texture->image_->allocation_);
        }
    }
}

void Model::process_node(const aiNode *node, const aiScene *scene) {
    meshes_.reserve(meshes_.size() + node->mNumMeshes);

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        meshes_.push_back(process_mesh(mesh, scene));
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        process_node(node->mChildren[i], scene);
    }
}

Mesh Model::process_mesh(const aiMesh *mesh, const aiScene *scene) {
    MeshData data;
    data.vertices_.reserve(mesh->mNumVertices);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        Vertex vertex{};

        vertex.position_ = {mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z};

        if (mesh->HasNormals()) {
            vertex.normal_ = {mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z};
        }

        if (mesh->HasTextureCoords(0)) {
            vertex.uv_ = {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y};
        } else {
            vertex.uv_ = {0.0f, 0.0f};
        }

        if (mesh->HasTangentsAndBitangents()) {
            const glm::vec3 tangent{mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z};
            const glm::vec3 bitangent{mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z};

            const float handedness = (glm::dot(glm::cross(vertex.normal_, tangent), bitangent) < 0.0f) ? -1.0f : 1.0f;
            vertex.tangent_ = glm::vec4(tangent, handedness);
        } else {
            vertex.tangent_ = glm::vec4(0.0f);
        }

        data.vertices_.push_back(vertex);
    }

    data.indices_.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace &face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j) {
            data.indices_.push_back(face.mIndices[j]);
        }
    }

    Material *material = nullptr;
    if (mesh->mMaterialIndex < materials_.size()) {
        material = &materials_[mesh->mMaterialIndex];
    }

    return Mesh(std::move(data), material);
}

Material Model::process_material(const aiScene *scene, const aiMaterial *ai_material) {
    Material material;

    // PBR base color and albedo, older formats fall back to diffuse
    material.base_color_ = load_material_texture(scene, ai_material, aiTextureType_BASE_COLOR);
    if (!material.base_color_) {
        material.base_color_ = load_material_texture(scene, ai_material, aiTextureType_DIFFUSE);
    }

    material.normal_ = load_material_texture(scene, ai_material, aiTextureType_NORMALS);
    if (!material.normal_) {
        material.normal_ = load_material_texture(scene, ai_material, aiTextureType_HEIGHT);
    }

    material.metallic_roughness_ = load_material_texture(scene, ai_material, aiTextureType_METALNESS);
    if (!material.metallic_roughness_) {
        material.metallic_roughness_ = load_material_texture(scene, ai_material, aiTextureType_DIFFUSE_ROUGHNESS);
    }
    if (!material.metallic_roughness_) {
        material.metallic_roughness_ = load_material_texture(scene, ai_material, aiTextureType_UNKNOWN);
    }

    material.occlusion_ = load_material_texture(scene, ai_material, aiTextureType_AMBIENT_OCCLUSION);
    if (!material.occlusion_) {
        material.occlusion_ = load_material_texture(scene, ai_material, aiTextureType_LIGHTMAP);
    }
    if (!material.occlusion_ && material.metallic_roughness_) {
        material.occlusion_ = material.metallic_roughness_;
    }

    material.emissive_ = load_material_texture(scene, ai_material, aiTextureType_EMISSION_COLOR);
    if (!material.emissive_) {
        material.emissive_ = load_material_texture(scene, ai_material, aiTextureType_EMISSIVE);
    }

    if (aiColor4D base_color; AI_SUCCESS == ai_material->Get(AI_MATKEY_BASE_COLOR, base_color)) {
        material.base_color_factor_ = {base_color.r, base_color.g, base_color.b, base_color.a};
    } else if (aiColor3D diffuse; AI_SUCCESS == ai_material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse)) {
        material.base_color_factor_ = {diffuse.r, diffuse.g, diffuse.b, 1.0f};
    }

    ai_material->Get(AI_MATKEY_METALLIC_FACTOR, material.metallic_factor_);
    ai_material->Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness_factor_);

    if (float emissive_intensity = 1.0f; AI_SUCCESS == ai_material->Get(
                                             AI_MATKEY_EMISSIVE_INTENSITY, emissive_intensity)) {
        material.emissive_strength_ = emissive_intensity;
    }

    return material;
}

std::shared_ptr<Texture> Model::load_material_texture(const aiScene *scene,
                                                      const aiMaterial *material,
                                                      const aiTextureType ai_type,
                                                      const unsigned int index) {
    if (material->GetTextureCount(ai_type) <= index) {
        return nullptr;
    }

    aiString ai_path;
    if (material->GetTexture(ai_type, index, &ai_path) != AI_SUCCESS) {
        return nullptr;
    }

    const std::string key = ai_path.C_Str();
    if (const auto cached = texture_cache_.find(key); cached != texture_cache_.end()) {
        return cached->second;
    }

    auto texture = std::make_shared<Texture>();
    const VkFormat format = determine_format(ai_type);

    // Check if the texture is embedded
    if (const auto *ai_embedded_texture = scene->GetEmbeddedTexture(ai_path.C_Str())) {
        texture->path_ = "embedded_" + key;

        // If mHeight == 0, this is the compressed file
        texture->image_ = context_->load_texture_memory(
            reinterpret_cast<unsigned char *>(ai_embedded_texture->pcData),
            ai_embedded_texture->mWidth,
            ai_embedded_texture->mHeight,
            format
        );
    } else {
        const std::filesystem::path texture_path = directory_ / ai_path.C_Str();
        texture->path_ = texture_path;
        texture->image_ = context_->load_texture(texture_path, format);
    }

    texture_cache_.emplace(key, texture);
    return texture;
}
