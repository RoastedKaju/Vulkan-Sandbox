#include <iostream>
#include <memory>

#include "utils.h"
#include "context.h"
#include "model.h"
#include "buffer.h"
#include "shader.h"
#include "pipeline.h"

constexpr uint32_t kWidth = 1280u;
constexpr uint32_t kHeight = 720u;

struct ShaderData {
    glm::mat4 projection_;
    glm::mat4 view_;
    glm::vec3 camera_;
    uint32_t bindless_cube_;
};

struct alignas(16) PushConstant {
    glm::mat4 model_;
    VkDeviceAddress data_address;
    uint32_t bindless_albedo_;
    uint32_t bindless_metallic_;
    uint32_t bindless_normal_;
};

struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 10.0f);
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);

    float yaw = -90.0f;
    float pitch = 0.0f;

    float speed_ = 5.0f;
    float sensitivity_ = 0.1f;

    void update() {
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }

    void process_keyboard(const bool *keys, const float dt) {
        const float velocity = speed_ * dt;

        if (keys[SDL_SCANCODE_W]) position += front * velocity;
        if (keys[SDL_SCANCODE_S]) position -= front * velocity;
        if (keys[SDL_SCANCODE_A]) position -= right * velocity;
        if (keys[SDL_SCANCODE_D]) position += right * velocity;
        if (keys[SDL_SCANCODE_SPACE]) position += worldUp * velocity;
        if (keys[SDL_SCANCODE_LCTRL]) position -= worldUp * velocity;
    }

    void process_mouse(const float xrel, const float yrel) {
        yaw += xrel * sensitivity_;
        pitch -= yrel * sensitivity_; // inverted

        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        update();
    }

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }
};

int main(int argc, char *argv[]) {
    Config config{
        .app_name_ = "PBR",
        .present_mode_ = VK_PRESENT_MODE_FIFO_KHR,
        .enable_validation_ = true
    };
    const auto ctx = std::make_unique<Context>(config);
    ctx->initialize();
    [[maybe_unused]] SDL_Window *window = ctx->create_window("PBR Example", kWidth, kHeight);
    SDL_SetWindowRelativeMouseMode(window, true);

    // load model
    Model tank_model{};
    tank_model.load(ctx.get(), "assets/models/khalid/scene.gltf");

    std::vector<MeshData> tank_meshes_data;
    std::vector<Buffer> vert_buffers;
    std::vector<Buffer> index_buffers;

    for (auto i = 0; i < tank_model.meshes().size(); ++i) {
        // copy data
        tank_meshes_data.push_back(tank_model.meshes().at(i).data());
        // vertices
        const VkDeviceSize v_buf_size = sizeof(Vertex) * tank_meshes_data.back().vertices_.size();
        BufferDesc v_buf_desc{
            .context = ctx.get(),
            .usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .size = v_buf_size
        };
        vert_buffers.emplace_back(Buffer{});
        vert_buffers.back().create(v_buf_desc);
        vert_buffers.back().update(tank_meshes_data.back().vertices_.data());
        // indices
        const VkDeviceSize i_buf_size = sizeof(uint32_t) * tank_meshes_data.back().indices_.size();
        BufferDesc i_buf_desc{
            .context = ctx.get(),
            .usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .size = i_buf_size
        };
        index_buffers.emplace_back(Buffer{});
        index_buffers.back().create(i_buf_desc);
        index_buffers.back().update(tank_meshes_data.back().indices_.data());
    }

    // cubemap texture
    std::unique_ptr<Image> sky_tex = ctx->load_cubemap(
        {
            "assets/textures/farm/right.png",
            "assets/textures/farm/left.png",
            "assets/textures/farm/top.png",
            "assets/textures/farm/bottom.png",
            "assets/textures/farm/front.png",
            "assets/textures/farm/back.png",
        });
    // default textures
    std::unique_ptr<Image> white_color = ctx->create_solid_texture(glm::u8vec4(255, 255, 255, 255),
                                                                   VK_FORMAT_R8G8B8A8_SRGB);
    std::unique_ptr<Image> flat_normal = ctx->create_solid_texture(glm::u8vec4(128, 128, 255, 255),
                                                                   VK_FORMAT_R8G8B8A8_UNORM);
    std::unique_ptr<Image> black_tex = ctx->create_solid_texture(glm::u8vec4(0, 50, 125, 255), VK_FORMAT_R8G8B8A8_UNORM);

    // per frame uniform buffer
    BufferDesc u_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = sizeof(ShaderData),
        .per_frame = true
    };
    Buffer uniform_buffer{};
    uniform_buffer.create(u_buf_desc);

    BufferDesc proc_u_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        .size = sizeof(ShaderData),
        .per_frame = true
    };
    Buffer proc_uniform_buffer{};
    proc_uniform_buffer.create(proc_u_buf_desc);

    // load shaders
    [[maybe_unused]] const VkShaderModule vert_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/pbr.vert.glsl",
        shaderc_vertex_shader);
    [[maybe_unused]] const VkShaderModule frag_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/pbr.frag.glsl",
        shaderc_fragment_shader);
    [[maybe_unused]] const VkShaderModule proc_vert_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/skybox.vert.glsl",
        shaderc_vertex_shader);
    [[maybe_unused]] const VkShaderModule proc_frag_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/skybox.frag.glsl",
        shaderc_fragment_shader);

    // create depth texture
    TextureDesc depth_tex_desc{};
    depth_tex_desc.dimension_ = {kWidth, kHeight};
    depth_tex_desc.mip_levels_ = 1;
    depth_tex_desc.aspect_ = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    depth_tex_desc.array_layers_ = 1;
    depth_tex_desc.depth_ = 1.0;
    depth_tex_desc.format_ = ctx->get_device_depth_format();
    depth_tex_desc.tiling_ = VK_IMAGE_TILING_OPTIMAL;
    depth_tex_desc.usage_ = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depth_tex_desc.prefer_dedicated_alloc_ = true;
    auto depth_texture = ctx->create_texture(depth_tex_desc);

    // create pipeline layout
    PipelineLayoutBuilder pipeline_layout_desc{};
    pipeline_layout_desc.add_descriptor_set_layout(ctx->get_texture_registry().get_layout());
    pipeline_layout_desc.add_push_constant(VK_SHADER_STAGE_VERTEX_BIT, sizeof(PushConstant));
    const PipelineLayout pipeline_layout = pipeline_layout_desc.build(ctx.get());

    // create solid pipeline
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
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal_)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv_)},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, tangent_)},
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
                                                 depth_texture->format_);

    // create skybox pipeline
    PipelineBuilder proc_pipeline_builder{};
    proc_pipeline_builder.add_shader(VK_SHADER_STAGE_VERTEX_BIT, proc_vert_shader);
    proc_pipeline_builder.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, proc_frag_shader);
    proc_pipeline_builder.set_vertex_layout({}, {});
    proc_pipeline_builder.set_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    proc_pipeline_builder.set_viewport(1, 1, true);
    proc_pipeline_builder.set_rasterization(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    proc_pipeline_builder.set_multisampling(VK_SAMPLE_COUNT_1_BIT);
    proc_pipeline_builder.set_depth_stencil(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);
    proc_pipeline_builder.set_color_blend(1, 0xF);
    VkPipeline proc_pipeline = proc_pipeline_builder.build(ctx.get(),
                                                           pipeline_layout,
                                                           {ctx->get_swap_chain().get_format()},
                                                           depth_texture->format_);

    // loop setup
    Uint64 last_time = SDL_GetPerformanceCounter();
    bool quit = false;

    // camera
    Camera camera{};
    camera.position = glm::vec3(0.0f, 0.0f, 10.0f);
    camera.update();

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
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                camera.process_mouse(event.motion.xrel, event.motion.yrel);
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                quit = true;
            }
        }

        const bool *keys = SDL_GetKeyboardState(nullptr);
        camera.process_keyboard(keys, delta_time);

        // update shader data
        ShaderData shader_data{};
        shader_data.projection_ = glm::perspective(glm::radians(45.0f),
                                                   1280.0f / 720.0f,
                                                   0.1f, 1000.0f);

        shader_data.projection_[1][1] *= -1.0f; // flip Y

        shader_data.view_ = camera.getViewMatrix();
        shader_data.camera_ = camera.position;

        [[maybe_unused]] auto time = SDL_GetTicks() / 1000.0f;

        ctx->acquire_command_buffer();
        {
            // skybox
            ShaderData proc_shader_data{};
            proc_shader_data.projection_ = shader_data.projection_;
            proc_shader_data.view_ = shader_data.view_;
            proc_shader_data.bindless_cube_ = sky_tex->bindless_index_;
            proc_uniform_buffer.update(&proc_shader_data);

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
            frame_buffer.color_images_[0] = ctx->get_current_swap_chain_image();

            ctx->begin_rendering(scene_pass, frame_buffer);
            {
                // draw procedural box
                ctx->bind_pipeline(proc_pipeline);
                ctx->bind_descriptor_set(pipeline_layout, ctx->get_texture_registry().get_set());
                PushConstant proc_pc{.data_address = proc_uniform_buffer.address()};
                ctx->cmd_push_constants(pipeline_layout, &proc_pc);
                ctx->draw(36);

                // model
                ctx->bind_pipeline(pipeline);
                for (auto i = 0; i < tank_model.meshes().size(); ++i) {
                    PushConstant pc{};
                    pc.data_address = uniform_buffer.address();
                    const auto &mat = tank_model.meshes().at(i).material();

                    auto transform = glm::mat4(1.0f);
                    transform = glm::translate(transform, glm::vec3(0.0f, 0.0f, 0.0f));
                    transform = glm::rotate(transform,
                                            glm::radians(45.0f * (time * 0.05f)), glm::vec3(0.0f, 1.0f, 0.0f));
                    transform = glm::rotate(transform, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
                    transform = glm::scale(transform, glm::vec3(1.0f, 1.0f, 1.0f));
                    pc.model_ = transform;
                    pc.bindless_albedo_ =
                            mat->base_color_
                                ? mat->base_color_->image_->bindless_index_
                                : white_color->bindless_index_;

                    pc.bindless_normal_ =
                            mat->normal_
                                ? mat->normal_->image_->bindless_index_
                                : flat_normal->bindless_index_;

                    pc.bindless_metallic_ =
                            mat->metallic_roughness_
                                ? mat->metallic_roughness_->image_->bindless_index_
                                : black_tex->bindless_index_;

                    shader_data.bindless_cube_ = sky_tex->bindless_index_;
                    uniform_buffer.update(&shader_data);

                    ctx->bind_vertex_buffer(vert_buffers.at(i).get());
                    ctx->bind_index_buffer(index_buffers.at(i).get());
                    ctx->cmd_push_constants(pipeline_layout, &pc);
                    ctx->draw_indexed(tank_meshes_data.at(i).indices_.size());
                }
            }
            ctx->end_rendering();
        }
        ctx->submit();

        if (ctx->get_swap_chain().is_swap_chain_dirty()) {
            ctx->recreate_swap_chain();
            // recreate depth texture
            ctx->destroy_image(depth_texture.get());
            depth_tex_desc.dimension_ = ctx->get_window_size();
            depth_texture = ctx->create_texture(depth_tex_desc);
        }
    }

    // waits for device to be idle
    ctx->wait_idle();
    // clean up resources
    for (auto i = 0; i < tank_model.meshes().size(); ++i) {
        vert_buffers.at(i).destroy();
        index_buffers.at(i).destroy();
    }
    uniform_buffer.destroy();
    proc_uniform_buffer.destroy();
    ctx->destroy_pipeline_layout(pipeline_layout);
    ctx->destroy_pipeline(pipeline);
    ctx->destroy_pipeline(proc_pipeline);
    ctx->destory_shader(vert_shader);
    ctx->destory_shader(frag_shader);
    ctx->destory_shader(proc_vert_shader);
    ctx->destory_shader(proc_frag_shader);
    ctx->destroy_image(depth_texture.get());
    tank_model.destroy_textures();
    ctx->destroy_image(sky_tex.get());
    ctx->destroy_image(white_color.get());
    ctx->destroy_image(flat_normal.get());
    ctx->destroy_image(black_tex.get());
    // destroy window, instance and device
    ctx->destroy();

    return EXIT_SUCCESS;
}
