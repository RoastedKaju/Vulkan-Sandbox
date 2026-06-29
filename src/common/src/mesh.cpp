#include "mesh.h"

#include "utils.h"
#include "model_importer.h"

void Mesh::load_mesh(const std::filesystem::path &path) {
    if (!std::filesystem::exists(path)) {
        std::printf("Model path is invalid.\n");
        exit(EXIT_FAILURE);
    }

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;

    std::string warnings{};
    std::string errors{};

    // check if .GLB or .GLTF
    if (path.extension() == ".glb") {
        check(loader.LoadBinaryFromFile(&model, &errors, &warnings, path.string()));
    } else {
        check(loader.LoadASCIIFromFile(&model, &errors, &warnings, path.string()));
    }

    if (!warnings.empty()) {
        std::printf("Warning: %s\n", warnings.c_str());
    }
    if (!errors.empty()) {
        std::printf("Errors: %s\n", errors.c_str());
    }

    // only load mesh at index 0
    for (const auto &primitive: model.meshes[0].primitives) {
        MeshData mesh_data = load_mesh_data(model, primitive);

        const auto vertex_offset = static_cast<uint32_t>(data_.vertices.size());

        data_.vertices.insert(data_.vertices.end(), mesh_data.vertices.begin(), mesh_data.vertices.end());

        for (const uint32_t index: mesh_data.indices) {
            data_.indices.push_back(index + vertex_offset);
        }
    }
}
