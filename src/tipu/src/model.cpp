#include "model.h"

#include <format>
#include <iostream>

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
    loaded_textures_.clear();

    process_node(scene->mRootNode, scene);
    return true;
}

void Model::process_node(const aiNode *node, const aiScene *scene) {
    meshes_.reserve(meshes_.size() + node->mNumMeshes);

    for (auto i = 0; i < node->mNumMeshes; ++i) {
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

    std::vector<Texture> textures;
    // const aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
    //
    // auto append = [&textures](std::vector<Texture> loaded) {
    //     textures.insert(textures.end(),
    //                     std::make_move_iterator(loaded.begin()),
    //                     std::make_move_iterator(loaded.end()));
    // };
    //
    // append(load_material_textures(material, aiTextureType_DIFFUSE, TextureType::Diffuse));
    // append(load_material_textures(material, aiTextureType_SPECULAR, TextureType::Specular));
    // append(load_material_textures(material, aiTextureType_NORMALS, TextureType::Normal));
    // append(load_material_textures(material, aiTextureType_HEIGHT, TextureType::Height));

    return Mesh(std::move(data), std::move(textures));
}

std::vector<Texture> Model::load_material_textures(const aiMaterial *material,
                                                   const aiTextureType ai_type,
                                                   const TextureType type) {
    std::vector<Texture> textures;
    const unsigned int count = material->GetTextureCount(ai_type);
    textures.reserve(count);

    for (unsigned int i = 0; i < count; ++i) {
        aiString ai_path;
        material->GetTexture(ai_type, i, &ai_path);

        const std::string key = ai_path.C_Str();
        if (const auto cached = loaded_textures_.find(key); cached != loaded_textures_.end()) {
            textures.push_back(cached->second);
            continue;
        }

        const std::filesystem::path texture_path = directory_ / ai_path.C_Str();

        Texture texture;
        texture.image_ = context_->load_texture(texture_path);
        texture.type_ = type;
        texture.path_ = texture_path;

        loaded_textures_.emplace(key, texture);
        textures.push_back(texture);
    }

    return textures;
}