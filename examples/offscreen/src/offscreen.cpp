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

    // off-screen render target
    TextureDesc color_tex_desc{};
    color_tex_desc.dimension_ = {kWidth, kHeight};
    color_tex_desc.mip_levels_ = 1;
    color_tex_desc.array_layers_ = 1;
    color_tex_desc.aspect_ = VK_IMAGE_ASPECT_COLOR_BIT;
    color_tex_desc.format_ = VK_FORMAT_R16G16B16A16_SFLOAT; // HDR-friendly; swapchain is usually 8-bit SRGB
    color_tex_desc.tiling_ = VK_IMAGE_TILING_OPTIMAL;
    color_tex_desc.usage_ = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    color_tex_desc.prefer_dedicated_alloc_ = true;
    const auto offscreen_color = ctx->create_texture(color_tex_desc);

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
            // Attachments
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

            ctx->blit_to_swapchain(*offscreen_color.get());
        }
        ctx->submit();

        if (ctx->get_swap_chain().is_swap_chain_dirty()) {
            ctx->recreate_swap_chain();
        }
    }

    // waits for device to be idle
    ctx->wait_idle();
    // clean up resources
    ctx->destroy_pipeline_layout(pipeline_layout);
    ctx->destroy_pipeline(pipeline);
    ctx->destory_shader(vert_shader);
    ctx->destory_shader(frag_shader);
    ctx->destroy_image(offscreen_color.get());
    // destroy window, instance and device
    ctx->destroy();

    return EXIT_SUCCESS;
}
