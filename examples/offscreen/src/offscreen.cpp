#include <iostream>
#include <memory>

#include "context.h"
#include "swap_chain.h"
#include "buffer.h"
#include "utils.h"
#include "shader.h"
#include "pipeline.h"

constexpr uint32_t kWidth = 1280u;
constexpr uint32_t kHeight = 720u;

struct PresentPushConstant {
    uint32_t tex_index;
};

int main(int argc, char *argv[]) {
    Config config{
        .app_name_ = "Offscreen",
        .present_mode_ = VK_PRESENT_MODE_FIFO_KHR,
        .enable_validation_ = true
    };
    const auto ctx = std::make_unique<Context>(config);
    if (!ctx->initialize()) {
        std::cout << "Failed to init context.\n";
        return EXIT_FAILURE;
    }

    [[maybe_unused]] SDL_Window *window = ctx->create_window("Offscreen Render Target", kWidth, kHeight);

    // shaders
    const VkShaderModule vert_shader = Shader::create_shader_module(ctx.get(),
                                                                    "assets/shaders/triangle.vert.glsl",
                                                                    shaderc_vertex_shader);
    const VkShaderModule frag_shader = Shader::create_shader_module(ctx.get(),
                                                                    "assets/shaders/triangle.frag.glsl",
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

    // pipeline layout
    PipelineLayoutBuilder pipeline_layout_desc{};
    const PipelineLayout pipeline_layout = pipeline_layout_desc.build(ctx.get());

    // pipeline
    PipelineBuilder pipeline_builder{};
    pipeline_builder.add_shader(VK_SHADER_STAGE_VERTEX_BIT, vert_shader);
    pipeline_builder.add_shader(VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader);
    pipeline_builder.set_input_assembly(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipeline_builder.set_viewport(1, 1, true);
    pipeline_builder.set_rasterization(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
    pipeline_builder.set_multisampling(VK_SAMPLE_COUNT_1_BIT);
    pipeline_builder.set_color_blend(1, 0xF);
    const VkPipeline pipeline = pipeline_builder.build(ctx.get(),
                                                       pipeline_layout,
                                                       {color_tex_desc.format_},
                                                       VK_FORMAT_UNDEFINED);

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

    bool quit = false;

    while (!quit) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                quit = true;
            }
        }

        ctx->acquire_command_buffer();
        {
            // 1st Pass
            Attachment scene_pass{};
            scene_pass.add_color(
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_STORE,
                {0.0f, 0.0f, 0.0f, 1.0f});

            // frame buffer
            FrameBuffer frame_buffer{};
            frame_buffer.color_images_[0] = offscreen_color.get();

            ctx->begin_rendering(scene_pass, frame_buffer);
            {
                ctx->bind_pipeline(pipeline);
                ctx->draw(3);
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
                PresentPushConstant pc{.tex_index = offscreen_color->bindless_index_};
                ctx->cmd_push_constants(present_pipeline_layout, &pc);
                ctx->draw(3);
            }
            ctx->end_rendering();
        }
        ctx->submit();

        if (ctx->get_swap_chain().is_swap_chain_dirty()) {
            ctx->recreate_swap_chain();

            ctx->destroy_image(offscreen_color.get());
            color_tex_desc.dimension_ = ctx->get_window_size();
            offscreen_color = ctx->create_texture(color_tex_desc);
        }
    }

    // waits for device to be idle
    ctx->wait_idle();
    // clean up resources
    ctx->destroy_pipeline_layout(pipeline_layout);
    ctx->destroy_pipeline(pipeline);
    ctx->destroy_pipeline_layout(present_pipeline_layout);
    ctx->destroy_pipeline(present_pipeline);
    ctx->destory_shader(vert_shader);
    ctx->destory_shader(frag_shader);
    ctx->destory_shader(present_vert);
    ctx->destory_shader(present_frag);
    ctx->destroy_image(offscreen_color.get());
    // destroy window, instance and device
    ctx->destroy();

    return EXIT_SUCCESS;
}
