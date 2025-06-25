
#include <ren/core/Application.h>
#include <ren/layers/ImGuiLayer.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_sdl2.h>
#include <ren/core/Entity.h>
#include <ren/assets/Mesh.h>

static ren::Application *g_application = nullptr;
namespace ren {

  Application &Application::get(void) { return *g_application; }
  Application::Application(const std::string &app_name, glm::uvec2 window_size) {
    g_application = this;
    // Initialize the SDL window
    SDL_Init(SDL_INIT_VIDEO);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    this->window =
        SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         window_size.x, window_size.y, window_flags);

    this->renderer = makeRef<Renderer>(this->window);


    this->sceneLayer = makeRef<SceneLayer>(*this);
    this->layerStack.pushLayer(sceneLayer);

    // Add the ImGuiLayer to the stack.
    this->imguiLayer = makeRef<ImGuiLayer>(*this);
    this->layerStack.pushLayer(imguiLayer);


    // scene.createEntity("Camera");
    // scene.createEntity("Cube");


    // auto s = scene.serialize();
    // std::cout << "Serialized scene:\n" << s << std::endl;
    // ren::loadObj("assets/test/meshes/unit_cube.obj");
    // ren::loadGLTF("assets/test/meshes/unit_cube.glb");
    // ren::loadGLTF("assets/test/meshes/suzanne.glb");
    // exit(0);
  }

  Application::~Application() {
    REN_PROFILE_FUNCTION();
    this->renderer->waitForIdle();

    // Clear the layer stack
    this->layerStack.clear();

    // Nuke the renderer.
    this->renderer.reset();

    // And finally, close the SDL window
    SDL_DestroyWindow(this->window);
    this->window = nullptr;
  }




  void Application::run() {
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastTime = startTime;
    SDL_Event e;

    auto &vulkan = ren::getVulkan();

    while (this->running) {
      int eventsHandled = 0;
      REN_PROFILE_SCOPE("Frame");


      auto currentTime = std::chrono::high_resolution_clock::now();
      float time =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime)
              .count();
      auto deltaTime =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime)
              .count();
      lastTime = currentTime;

      {
        bool windowResized = false;
        REN_PROFILE_SCOPE("SDL Poll");
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
          eventsHandled++;
          // close the window when user alt-f4s or clicks the X button
          if (e.type == SDL_QUIT) {
            this->running = false;
            break;
          }

          Event renEvent(e);
          layerStack.dispatchEvent(renEvent);
        }

        REN_PROFILE_COUNTER("SDL Events", eventsHandled);
      }

      if (!running) break;

      // Get a frame from the swapchain.

      renderer->beginFrame();
      auto &frame = ren::getFrameData();

      // VkPhysicalDeviceProperties deviceProps;
      // vkGetPhysicalDeviceProperties(vulkan.physical_device, &deviceProps);
      // auto timestamps = frame.getQueryResults();
      // uint64_t ticksDiff = timestamps[1] - timestamps[0];

      // // Convert to nanoseconds
      // double nanoseconds = (double)ticksDiff * deviceProps.limits.timestampPeriod;

      // // Convert to milliseconds
      // double milliseconds = nanoseconds / 1000000.0;

      // fmt::println("Frame {}: {} ms", frame.frameIndex, milliseconds);
      // REN_PROFILE_RECORD_GPUTIME("GPU Pipeline", milliseconds);
      // REN_PROFILE_COUNTER("Pipeline", milliseconds);

      // vkCmdResetQueryPool(frame.commandBuffer, frame.queryPool, 0, frame.query_count);
      // vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      // frame.queryPool, 0);

      // Render the scene.


      // Render with ImGui.
      renderer->withPass(renderer->getRenderPass(), *frame.renderTarget, [&]() {
        REN_PROFILE_SCOPE("ImGui Render");
        {
          REN_PROFILE_SCOPE("ImGui New Frame");
          ImGui_ImplVulkan_NewFrame();
          ImGui_ImplSDL2_NewFrame();

          ImGui::NewFrame();
        }


        layerStack.onImGuiRender(deltaTime);

        {
          REN_PROFILE_SCOPE("ImGui Render Draw Data");
          ImGui::Render();
          // Gross leakage.
          ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ren::getFrameData().commandBuffer);
        }
      });


      renderer->endFrame();



      // Update the layers.
      layerStack.onUpdate(deltaTime);
    }
  }


}  // namespace ren