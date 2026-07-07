#include <iostream>
#include <memory>

#include "utils.h"
#include "context.h"
#include "mesh.h"
#include "buffer.h"
#include "shader.h"
#include "pipeline.h"

constexpr uint32_t kWidth = 1280u;
constexpr uint32_t kHeight = 720u;

struct ShaderData {
    glm::mat4 projection_;
    glm::mat4 view_;
    glm::mat4 model_;
    glm::vec3 camera_;
    uint32_t bindless_texture_index_;
    uint32_t bindless_cube_index_;
};

struct PushConstant {
    VkDeviceAddress data_address;
};

struct Camera {
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 10.0f);
    glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);

    float yaw = -90.0f; // facing -Z initially
    float pitch = 0.0f;

    float moveSpeed = 5.0f; // units per second
    float mouseSens = 0.1f; // degrees per pixel

    void update() {
        glm::vec3 newFront;
        newFront.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        newFront.y = sin(glm::radians(pitch));
        newFront.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(newFront);
        right = glm::normalize(glm::cross(front, worldUp));
        up = glm::normalize(glm::cross(right, front));
    }

    void process_keyboard(const bool *keys, float dt) {
        float velocity = moveSpeed * dt;

        if (keys[SDL_SCANCODE_W]) position += front * velocity;
        if (keys[SDL_SCANCODE_S]) position -= front * velocity;
        if (keys[SDL_SCANCODE_A]) position -= right * velocity;
        if (keys[SDL_SCANCODE_D]) position += right * velocity;
        if (keys[SDL_SCANCODE_SPACE]) position += worldUp * velocity;
        if (keys[SDL_SCANCODE_LCTRL]) position -= worldUp * velocity;
    }

    void process_mouse(float xrel, float yrel) {
        yaw += xrel * mouseSens;
        pitch -= yrel * mouseSens; // inverted so mouse-up looks up

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
        .app_name_ = "Model Viewer",
        .present_mode_ = VK_PRESENT_MODE_FIFO_KHR,
        .enable_validation_ = true
    };
    const auto ctx = std::make_unique<Context>(config);
    ctx->initialize();
    [[maybe_unused]] SDL_Window *window = ctx->create_window("Model Viewer", kWidth, kHeight);
    SDL_SetWindowRelativeMouseMode(window, true);

    // load model
    Mesh loaded_mesh{};
    loaded_mesh.load_mesh(("assets/models/cube.glb"));

    // load texture
    std::unique_ptr<Image> camo_tex = ctx->load_texture("assets/textures/camo.jpg");
    std::unique_ptr<Image> sky_tex = ctx->load_cubemap(
        {
            "assets/textures/skybox/right.jpg",
            "assets/textures/skybox/left.jpg",
            "assets/textures/skybox/top.jpg",
            "assets/textures/skybox/bottom.jpg",
            "assets/textures/skybox/front.jpg",
            "assets/textures/skybox/back.jpg",
        });

    // buffers for model
    const VkDeviceSize v_buf_size = sizeof(Vertex) * loaded_mesh.data().vertices_.size();
    const VkDeviceSize i_buf_size = sizeof(uint32_t) * loaded_mesh.data().indices_.size();

    // vertex buffer
    BufferDesc v_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .size = v_buf_size
    };
    Buffer vertex_buffer{};
    vertex_buffer.create(v_buf_desc);
    vertex_buffer.update(loaded_mesh.data().vertices_.data());

    // index buffer
    BufferDesc i_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .size = i_buf_size
    };
    Buffer index_buffer{};
    index_buffer.create(i_buf_desc);
    index_buffer.update(loaded_mesh.data().indices_.data());

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
        "assets/shaders/environment.vert.glsl",
        shaderc_vertex_shader);
    [[maybe_unused]] const VkShaderModule frag_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/environment.frag.glsl",
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
            // camo draw
            auto transform = glm::mat4(1.0f);
            transform = glm::translate(transform, glm::vec3(0.0f, 0.0f, 0.0f));
            transform = glm::rotate(transform, glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(1.0f, 1.0f, 1.0f));
            shader_data.model_ = transform;
            // shader_data.bindless_texture_index_ = camo_tex->bindless_index_;
            shader_data.bindless_cube_index_ = sky_tex->bindless_index_;
            uniform_buffer.update(&shader_data);

            // proc draw
            ShaderData proc_shader_data{};
            proc_shader_data.projection_ = shader_data.projection_;
            proc_shader_data.view_ = shader_data.view_;
            proc_shader_data.bindless_cube_index_ = sky_tex->bindless_index_;
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
                ctx->bind_vertex_buffer(vertex_buffer.get());
                ctx->bind_index_buffer(index_buffer.get());
                PushConstant pc{.data_address = uniform_buffer.address()};
                ctx->cmd_push_constants(pipeline_layout, &pc);
                ctx->draw_indexed(loaded_mesh.data().indices_.size());
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
    vertex_buffer.destroy();
    index_buffer.destroy();
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
    ctx->destroy_image(camo_tex.get());
    ctx->destroy_image(sky_tex.get());
    // destroy window, instance and device
    ctx->destroy();

    return EXIT_SUCCESS;
}
