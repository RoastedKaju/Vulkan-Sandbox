#include <iostream>
#include <memory>

#include "utils.h"
#include "context.h"
#include "mesh.h"
#include "buffer.h"
#include "shader.h"
#include "pipeline.h"
#include "texture_descriptor_set.h"

constexpr uint32_t kWidth = 1280u;
constexpr uint32_t kHeight = 720u;

struct ShaderData {
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 model;
};

int main(int argc, char *argv[]) {
    Config config{
        .app_name_ = "Model Viewer",
        .present_mode_ = VK_PRESENT_MODE_FIFO_KHR,
        .enable_validation_ = true
    };

    const auto ctx = std::make_unique<Context>(config);
    ctx->initialize();
    [[maybe_unused]] SDL_Window *window = ctx->create_window("Model Viewer", kWidth, kHeight);

    // load model
    Mesh loaded_mesh{};
    loaded_mesh.load_mesh(("assets/models/tank.glb"));

    // buffers for model
    const VkDeviceSize v_buf_size = sizeof(Vertex) * loaded_mesh.data().vertices.size();
    const VkDeviceSize i_buf_size = sizeof(uint32_t) * loaded_mesh.data().indices.size();

    // vertex buffer
    BufferDesc v_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .size = v_buf_size
    };
    Buffer vertex_buffer{};
    vertex_buffer.create(v_buf_desc);
    vertex_buffer.update(loaded_mesh.data().vertices.data());

    // index buffer
    BufferDesc i_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .size = i_buf_size
    };
    Buffer index_buffer{};
    index_buffer.create(i_buf_desc);
    index_buffer.update(loaded_mesh.data().indices.data());

    // per frame uniform buffer
    BufferDesc u_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = sizeof(ShaderData),
        .per_frame = true
    };
    Buffer uniform_buffer{};
    uniform_buffer.create(u_buf_desc);

    // load shaders
    [[maybe_unused]] const VkShaderModule vert_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/vert.glsl",
        shaderc_vertex_shader);
    [[maybe_unused]] const VkShaderModule frag_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/frag.glsl",
        shaderc_fragment_shader);

    // texture descriptor set
    TextureDescriptorSet descriptor_set{};
    descriptor_set.create(ctx->get_device(), 64);

    // create depth texture
    TextureDesc depth_tex_desc{};
    depth_tex_desc.dimension_ = {kWidth, kHeight};
    depth_tex_desc.mip_levels_ = 1;
    depth_tex_desc.aspect_ = VK_IMAGE_ASPECT_DEPTH_BIT;
    depth_tex_desc.array_layers_ = 1;
    depth_tex_desc.depth_ = 1.0;
    depth_tex_desc.format_ = ctx->get_device_depth_format();
    depth_tex_desc.tiling_ = VK_IMAGE_TILING_OPTIMAL;
    depth_tex_desc.usage_ = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    auto depth_texture = ctx->create_texture(depth_tex_desc);

    // create pipeline layout
    PipelineLayoutBuilder pipeline_layout_desc{};
    pipeline_layout_desc.add_descriptor_set_layout(descriptor_set.layout());
    pipeline_layout_desc.add_push_constant(VK_SHADER_STAGE_VERTEX_BIT, sizeof(VkDeviceAddress));
    const VkPipelineLayout pipeline_layout = pipeline_layout_desc.build(ctx.get());

    // create pipeline
    PipelineBuilder pipeline_builder{};
    pipeline_builder.add_shader(VK_SHADER_STAGE_VERTEX_BIT, vert_shader);
    pipeline_builder.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader);
    constexpr VkVertexInputBindingDescription vertex_binding{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    const std::vector<VkVertexInputAttributeDescription> vertex_attributes{
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)},
    };
    pipeline_builder.set_vertex_layout(vertex_binding, vertex_attributes);
    pipeline_builder.set_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipeline_builder.set_viewport(1, 1, true);
    pipeline_builder.set_rasterization(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipeline_builder.set_multisampling(VK_SAMPLE_COUNT_1_BIT);
    pipeline_builder.set_depth_stencil(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipeline_builder.set_color_blend(1, 0xF);
    VkPipeline pipeline = pipeline_builder.build(ctx.get(),
                                                 pipeline_layout,
                                                 {ctx->get_swap_chain().get_format()},
                                                 depth_texture->format);

    // loop setup
    Uint64 last_time = SDL_GetPerformanceCounter();
    bool quit = false;

    // super loop
    while (!quit) {
        Uint64 current_time = SDL_GetPerformanceCounter();
        [[maybe_unused]] double delta_time = static_cast<double>(current_time - last_time) /
                                             static_cast<double>(SDL_GetPerformanceFrequency());
        last_time = current_time;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
        }

        // update shader data
        ShaderData shader_data{};
        shader_data.projection = glm::perspective(glm::radians(45.0f),
                                                  1280.0f / 720.0f,
                                                  0.1f, 1000.0f);
        shader_data.projection[1][1] *= -1.0f; // flip Y

        shader_data.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                       glm::vec3(0.0f, 0.0f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));

        auto time = SDL_GetTicks() / 1000.0f;

        ctx->acquire_command_buffer();
        {
            uint32_t frame_index = ctx->get_frame_index();

            auto transform = glm::mat4(1.0f);

            transform = glm::translate(transform, glm::vec3(0.0f, 0.0f, 0.0f));
            transform = glm::rotate(transform, glm::radians(45.0f * time), glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(1.0f, 1.0f, 1.0f));
            shader_data.model = transform;

            uniform_buffer.update(&shader_data); // upload data to buffer on GPU

            // attachments
            Attachment scene_pass{};
            scene_pass.add_color(
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_STORE,
                {0.0f, 0.0f, 0.0f, 1.0f}
            );
            scene_pass.set_depth(
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_DONT_CARE
            );

            FrameBuffer frame_buffer{};
            frame_buffer.depth_image_ = depth_texture.get();

            ctx->begin_rendering(scene_pass, frame_buffer);
            {
                ctx->bind_pipeline(pipeline);
                ctx->bind_descriptor_set(pipeline_layout, descriptor_set.get());
                ctx->bind_vertex_buffer(vertex_buffer.get());
                ctx->bind_index_buffer(index_buffer.get());
                ctx->cmd_push_constants(pipeline_layout, uniform_buffer.address());
                ctx->draw_indexed(loaded_mesh.data().indices.size());
            }
            ctx->end_rendering();
        }
        ctx->submit();
    }

    return EXIT_SUCCESS;
}
