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
        .app_name_ = "Animation Example",
        .present_mode_ = VK_PRESENT_MODE_FIFO_KHR,
        .enable_validation_ = true
    };
    const auto ctx = std::make_unique<Context>(config);
    if (!ctx->initialize()) {
        std::cout << "Failed to init context.\n";
        return EXIT_FAILURE;
    }

    [[maybe_unused]] SDL_Window *window = ctx->create_window("Animation Example", kWidth, kHeight);

    // shaders
    const VkShaderModule vert_shader = Shader::create_shader_module(ctx.get(),
                                                                    "assets/shaders/triangle.vert.glsl",
                                                                    shaderc_vertex_shader);
    const VkShaderModule frag_shader = Shader::create_shader_module(ctx.get(),
                                                                    "assets/shaders/triangle.frag.glsl",
                                                                    shaderc_fragment_shader);

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
            // Attachments
            Attachment scene_pass{};
            scene_pass.add_color(
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
                VK_ATTACHMENT_LOAD_OP_CLEAR,
                VK_ATTACHMENT_STORE_OP_STORE,
                {0.0f, 0.0f, 0.0f, 1.0f});

            // frame buffer
            FrameBuffer frame_buffer{};
            frame_buffer.color_images_[0] = ctx->get_current_swap_chain_image();

            ctx->begin_rendering(scene_pass, frame_buffer);
            {
                ctx->bind_pipeline(pipeline);
                ctx->draw(3);
            }
            ctx->end_rendering();
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
    // destroy window, instance and device
    ctx->destroy();

    return EXIT_SUCCESS;
}
