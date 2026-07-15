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
constexpr VkSampleCountFlagBits kMsaaSamples = VK_SAMPLE_COUNT_4_BIT;

struct ShaderData {
    glm::mat4 projection_;
    glm::mat4 view_;
    glm::vec3 camera_;
    uint32_t bindless_cube_;
};

struct alignas(16) PushConstant {
    glm::mat4 model_;
    VkDeviceAddress data_address_;
    uint32_t bindless_albedo_;
    uint32_t bindless_metallic_;
    uint32_t bindless_normal_;
};

struct PresentPushConstant {
    uint32_t color_;
};

struct Camera {
    glm::vec3 position_ = glm::vec3(0.0f, 0.0f, 10.0f);
    glm::vec3 front_ = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 up_ = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 world_up_ = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right_ = glm::vec3(1.0f, 0.0f, 0.0f);

    float yaw_ = -90.0f; // facing -Z initially
    float pitch_ = 0.0f;

    float speed_ = 5.0f; // units per second
    float sen_ = 0.1f; // degrees per pixel

    void update() {
        glm::vec3 new_front;
        new_front.x = cos(glm::radians(yaw_)) * cos(glm::radians(pitch_));
        new_front.y = sin(glm::radians(pitch_));
        new_front.z = sin(glm::radians(yaw_)) * cos(glm::radians(pitch_));
        front_ = glm::normalize(new_front);
        right_ = glm::normalize(glm::cross(front_, world_up_));
        up_ = glm::normalize(glm::cross(right_, front_));
    }

    void process_keyboard(const bool *keys, const float dt) {
        const float velocity = speed_ * dt;

        if (keys[SDL_SCANCODE_W]) position_ += front_ * velocity;
        if (keys[SDL_SCANCODE_S]) position_ -= front_ * velocity;
        if (keys[SDL_SCANCODE_A]) position_ -= right_ * velocity;
        if (keys[SDL_SCANCODE_D]) position_ += right_ * velocity;
        if (keys[SDL_SCANCODE_SPACE]) position_ += world_up_ * velocity;
        if (keys[SDL_SCANCODE_LCTRL]) position_ -= world_up_ * velocity;
    }

    void process_mouse(const float xrel, const float yrel) {
        yaw_ += xrel * sen_;
        pitch_ -= yrel * sen_; // inverted so mouse-up looks up

        if (pitch_ > 89.0f) pitch_ = 89.0f;
        if (pitch_ < -89.0f) pitch_ = -89.0f;

        update();
    }

    glm::mat4 get_view_matrix() const {
        return glm::lookAt(position_, position_ + front_, up_);
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
    Model gun_model{};
    gun_model.load(ctx.get(), "assets/models/gun.glb");

    const auto gun_verts = gun_model.meshes().at(0).data().vertices_;
    const auto gun_indices = gun_model.meshes().at(0).data().indices_;


    // load skybox textures
    std::unique_ptr<Image> sky_tex = ctx->load_cubemap(
        {
            "assets/textures/farm/right.png",
            "assets/textures/farm/left.png",
            "assets/textures/farm/top.png",
            "assets/textures/farm/bottom.png",
            "assets/textures/farm/front.png",
            "assets/textures/farm/back.png",
        });

    // buffers for model
    const VkDeviceSize v_buf_size = sizeof(Vertex) * gun_verts.size();
    const VkDeviceSize i_buf_size = sizeof(uint32_t) * gun_indices.size();

    // vertex buffer
    BufferDesc v_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .size = v_buf_size
    };
    Buffer vertex_buffer{};
    vertex_buffer.create(v_buf_desc);
    vertex_buffer.update(gun_verts.data());

    // index buffer
    BufferDesc i_buf_desc{
        .context = ctx.get(),
        .usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .size = i_buf_size
    };
    Buffer index_buffer{};
    index_buffer.create(i_buf_desc);
    index_buffer.update(gun_indices.data());

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
    [[maybe_unused]] const VkShaderModule sky_vert_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/skybox.vert.glsl",
        shaderc_vertex_shader);
    [[maybe_unused]] const VkShaderModule sky_frag_shader = Shader::create_shader_module(ctx.get(),
        "assets/shaders/skybox.frag.glsl",
        shaderc_fragment_shader);
    const VkShaderModule present_vert = Shader::create_shader_module(ctx.get(),
                                                                     "assets/shaders/present.vert.glsl",
                                                                     shaderc_vertex_shader);
    const VkShaderModule present_frag = Shader::create_shader_module(ctx.get(),
                                                                     "assets/shaders/present.frag.glsl",
                                                                     shaderc_fragment_shader);

    // off-screen render target
    TextureDesc color_tex_desc{};
    color_tex_desc.dimension_ = {kWidth, kHeight};
    color_tex_desc.mip_levels_ = 1;
    color_tex_desc.array_layers_ = 1;
    color_tex_desc.aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
    color_tex_desc.format_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    color_tex_desc.tiling_ = VK_IMAGE_TILING_OPTIMAL;
    color_tex_desc.usage_ = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    color_tex_desc.prefer_dedicated_alloc_ = true;
    auto offscreen_color = ctx->create_texture(color_tex_desc);

    // MSAA
    TextureDesc msaa_color_desc = color_tex_desc;
    msaa_color_desc.usage_ = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    msaa_color_desc.samples_ = kMsaaSamples;
    auto msaa_color = ctx->create_texture(msaa_color_desc);

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
    depth_tex_desc.samples_ = kMsaaSamples;
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
    pipeline_builder.set_multisampling(kMsaaSamples);
    pipeline_builder.set_depth_stencil(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);
    pipeline_builder.set_color_blend(1, 0xF);
    VkPipeline pipeline = pipeline_builder.build(ctx.get(),
                                                 pipeline_layout,
                                                 {offscreen_color->format_},
                                                 depth_texture->format_);

    // create skybox pipeline
    PipelineBuilder sky_pipeline_builder{};
    sky_pipeline_builder.add_shader(VK_SHADER_STAGE_VERTEX_BIT, sky_vert_shader);
    sky_pipeline_builder.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, sky_frag_shader);
    sky_pipeline_builder.set_vertex_layout({}, {});
    sky_pipeline_builder.set_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    sky_pipeline_builder.set_viewport(1, 1, true);
    sky_pipeline_builder.set_rasterization(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    sky_pipeline_builder.set_multisampling(kMsaaSamples);
    sky_pipeline_builder.set_depth_stencil(true, false, VK_COMPARE_OP_LESS_OR_EQUAL);
    sky_pipeline_builder.set_color_blend(1, 0xF);
    VkPipeline sky_pipeline = sky_pipeline_builder.build(ctx.get(),
                                                         pipeline_layout,
                                                         {offscreen_color->format_},
                                                         depth_texture->format_);

    // present pipeline
    PipelineLayoutBuilder present_layout_desc{};
    present_layout_desc.add_descriptor_set_layout(ctx->get_texture_registry().get_layout());
    present_layout_desc.add_push_constant(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PresentPushConstant));
    const PipelineLayout present_pipeline_layout = present_layout_desc.build(ctx.get());

    PipelineBuilder present_pipeline_builder{};
    present_pipeline_builder.add_shader(VK_SHADER_STAGE_VERTEX_BIT, present_vert);
    present_pipeline_builder.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, present_frag);
    present_pipeline_builder.set_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    present_pipeline_builder.set_viewport(1, 1, true);
    present_pipeline_builder.set_rasterization(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    present_pipeline_builder.set_multisampling(VK_SAMPLE_COUNT_1_BIT);
    present_pipeline_builder.set_color_blend(1, 0xF);
    const VkPipeline present_pipeline = present_pipeline_builder.build(ctx.get(),
                                                                       present_pipeline_layout,
                                                                       {ctx->get_swap_chain().get_format()},
                                                                       VK_FORMAT_UNDEFINED);

    // loop setup
    Uint64 last_time = SDL_GetPerformanceCounter();
    bool quit = false;

    // camera
    Camera camera{};
    camera.position_ = glm::vec3(0.0f, 0.0f, 10.0f);
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

        shader_data.view_ = camera.get_view_matrix();
        shader_data.camera_ = camera.position_;

        [[maybe_unused]] auto time = SDL_GetTicks() / 1000.0f;

        ctx->acquire_command_buffer();
        {
            auto transform = glm::mat4(1.0f);
            transform = glm::translate(transform, glm::vec3(0.0f, 0.0f, 0.0f));
            transform = glm::rotate(transform, glm::radians(45.0f * (time * 0.05f)), glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::rotate(transform, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(1.0f, 1.0f, 1.0f));
            shader_data.bindless_cube_ = sky_tex->bindless_index_;
            uniform_buffer.update(&shader_data);

            ShaderData sky_shader_data{};
            sky_shader_data.projection_ = shader_data.projection_;
            sky_shader_data.view_ = shader_data.view_;
            sky_shader_data.bindless_cube_ = sky_tex->bindless_index_;
            proc_uniform_buffer.update(&sky_shader_data);

            // 1st Pass
            Attachment scene_pass{};
            scene_pass.add_color(
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_DONT_CARE,
                {0.0f, 0.0f, 0.0f, 1.0f},
                VK_RESOLVE_MODE_AVERAGE_BIT,
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL
            );
            scene_pass.set_depth(
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_DONT_CARE
            );

            FrameBuffer frame_buffer{};
            frame_buffer.depth_image_ = depth_texture.get();
            frame_buffer.color_images_[0] = msaa_color.get();
            frame_buffer.resolve_images_[0] = offscreen_color.get();

            ctx->begin_rendering(scene_pass, frame_buffer);
            {
                // draw procedural box
                ctx->bind_pipeline(sky_pipeline);
                ctx->bind_descriptor_set(pipeline_layout, ctx->get_texture_registry().get_set());
                PushConstant proc_pc{.data_address_ = proc_uniform_buffer.address()};
                ctx->cmd_push_constants(pipeline_layout, &proc_pc);
                ctx->draw(36);

                // model
                ctx->bind_pipeline(pipeline);
                ctx->bind_vertex_buffer(vertex_buffer.get());
                ctx->bind_index_buffer(index_buffer.get());
                PushConstant pc{.data_address_ = uniform_buffer.address()};
                const auto &material = gun_model.meshes().at(0).material();
                pc.model_ = transform;
                pc.bindless_albedo_ = material->base_color_->image_->bindless_index_;
                pc.bindless_metallic_ = material->metallic_roughness_->image_->bindless_index_;
                pc.bindless_normal_ = material->normal_->image_->bindless_index_;
                ctx->cmd_push_constants(pipeline_layout, &pc);
                ctx->draw_indexed(gun_indices.size());
            }
            ctx->end_rendering();

            ctx->present(*offscreen_color.get());

            // 2nd Pass
            Attachment present_pass{};
            present_pass.add_color(
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                VK_ATTACHMENT_STORE_OP_STORE,
                {0.0f, 0.0f, 0.0f, 1.0f});

            FrameBuffer present_frame_buffer{};
            present_frame_buffer.color_images_[0] = ctx->get_current_swap_chain_image();

            ctx->begin_rendering(present_pass, present_frame_buffer);
            {
                ctx->bind_pipeline(present_pipeline);
                ctx->bind_descriptor_set(present_pipeline_layout, ctx->get_texture_registry().get_set());
                PresentPushConstant pc{.color_ = offscreen_color->bindless_index_};
                ctx->cmd_push_constants(present_pipeline_layout, &pc);
                ctx->draw(3);
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
            // recreate color texture
            ctx->destroy_image(offscreen_color.get());
            color_tex_desc.dimension_ = ctx->get_window_size();
            offscreen_color = ctx->create_texture(color_tex_desc);
            // recreate msaa texture
            ctx->destroy_image(msaa_color.get());
            msaa_color_desc.dimension_ = ctx->get_window_size();
            msaa_color = ctx->create_texture(msaa_color_desc);
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
    ctx->destroy_pipeline(sky_pipeline);
    ctx->destroy_pipeline_layout(present_pipeline_layout);
    ctx->destroy_pipeline(present_pipeline);
    ctx->destory_shader(vert_shader);
    ctx->destory_shader(frag_shader);
    ctx->destory_shader(sky_vert_shader);
    ctx->destory_shader(sky_frag_shader);
    ctx->destory_shader(present_vert);
    ctx->destory_shader(present_frag);
    ctx->destroy_image(depth_texture.get());
    ctx->destroy_image(offscreen_color.get());
    ctx->destroy_image(sky_tex.get());
    ctx->destroy_image(msaa_color.get());
    gun_model.destroy_textures();
    // destroy window, instance and device
    ctx->destroy();

    return EXIT_SUCCESS;
}
