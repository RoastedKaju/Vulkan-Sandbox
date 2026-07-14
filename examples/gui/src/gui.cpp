#include <iostream>
#include <memory>

#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include "context.h"
#include "swap_chain.h"
#include "buffer.h"
#include "utils.h"
#include "shader.h"
#include "pipeline.h"
#include "ui.h"

constexpr uint32_t kWidth = 1280u;
constexpr uint32_t kHeight = 720u;

int main(int argc, char *argv[]) {
    Config config{
        .app_name_ = "GUI Example",
        .present_mode_ = VK_PRESENT_MODE_FIFO_KHR,
        .enable_validation_ = true
    };
    const auto ctx = std::make_unique<Context>(config);
    if (!ctx->initialize()) {
        std::cout << "Failed to init context.\n";
        return EXIT_FAILURE;
    }

    [[maybe_unused]] SDL_Window *window = ctx->create_window("ImGUI", kWidth, kHeight);

    // Imgui context
    const auto imgui_ctx = std::make_unique<UI>();
    imgui_ctx->create_imgui_context(ctx.get());

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
            ImGui_ImplSDL3_ProcessEvent(&event);

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

                imgui_ctx->begin_frame(frame_buffer);
                static bool vsync = true;
                static bool wireframe = false;
                static bool bloom = true;
                static bool shadows = true;
                static float exposure = 1.0f;
                static float gamma = 2.2f;
                static float light_intensity = 5.0f;
                static float clear_color[3] = {0.08f, 0.09f, 0.11f};
                static int selected_object = 0;

                ImGui::SetNextWindowSize(ImVec2(420, 650), ImGuiCond_FirstUseEver);

                ImGui::Begin("Tipu Renderer GUI Example");

                ImGui::Text("Rendering");
                ImGui::Separator();

                ImGui::Text("Backend: Vulkan 1.3");
                ImGui::Text("Renderer: Dynamic Rendering");
                ImGui::Text("Resolution: 1280 x 720");
                ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
                ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

                ImGui::Spacing();

                ImGui::Text("Renderer Settings");
                ImGui::Separator();

                ImGui::Checkbox("VSync", &vsync);
                ImGui::Checkbox("Wireframe", &wireframe);
                ImGui::Checkbox("Bloom", &bloom);
                ImGui::Checkbox("Shadows", &shadows);

                ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
                ImGui::SliderFloat("Gamma", &gamma, 1.0f, 3.0f);
                ImGui::SliderFloat("Light Intensity", &light_intensity, 0.0f, 20.0f);

                ImGui::ColorEdit3("Clear Color", clear_color);

                ImGui::Spacing();

                ImGui::Text("Scene");
                ImGui::Separator();

                const char *objects[] = {
                    "Triangle",
                    "Cube",
                    "Sphere",
                    "Suzanne",
                    "Sponza"
                };

                ImGui::Combo("Selected Object", &selected_object, objects, IM_ARRAYSIZE(objects));

                ImGui::Button("Reload Shaders");
                ImGui::SameLine();
                ImGui::Button("Take Screenshot");

                ImGui::Spacing();

                if (ImGui::CollapsingHeader("GPU Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::BulletText("Draw Calls: 1");
                    ImGui::BulletText("Vertices: 3");
                    ImGui::BulletText("Pipelines: 1");
                    ImGui::BulletText("Descriptor Sets: 2");
                    ImGui::BulletText("VRAM Usage: 42 MB");
                }

                if (ImGui::CollapsingHeader("Application Log")) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[Info] Vulkan initialized");
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "[Info] Swapchain created");
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "[Warn] Validation layers enabled");
                }

                ImGui::Spacing();

                ImGui::ProgressBar(
                    (sinf(static_cast<float>(ImGui::GetTime())) + 1.0f) * 0.5f,
                    ImVec2(-FLT_MIN, 0),
                    "GPU Workload");

                ImGui::End();
                imgui_ctx->end_frame();
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
    imgui_ctx->shutdown();
    // clean up resources
    ctx->destroy_pipeline_layout(pipeline_layout);
    ctx->destroy_pipeline(pipeline);
    ctx->destory_shader(vert_shader);
    ctx->destory_shader(frag_shader);
    // destroy window, instance and device
    ctx->destroy();

    return EXIT_SUCCESS;
}
