#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class Mesh {
public:
    void load_mesh(const std::filesystem::path &path);

    MeshData &data() { return data_; }

private:
    MeshData data_;
};
