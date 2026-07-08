#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position_;
    glm::vec3 normal_;
    glm::vec2 uv_;
    glm::vec4 tangent_;
};

struct MeshData {
    std::vector<Vertex> vertices_;
    std::vector<uint32_t> indices_;
};

class Mesh {
public:
    void load_mesh(const std::filesystem::path &path);

    MeshData &data() { return data_; }

private:
    MeshData data_;
};
