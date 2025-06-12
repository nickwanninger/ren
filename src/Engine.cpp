#include <ren/Engine.h>

#include <stb/stb_image.h>


#include <imgui_impl_sdl2.h>

ren::Engine::Engine(const std::string& app_name, glm::uvec2 window_size)
    : app_name(app_name) {
  // First, initialize SDL with video and vulkan support.
  SDL_Init(SDL_INIT_VIDEO);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  // Then, create a window with the specified name and size.
  this->window =
      SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       window_size.x, window_size.y, window_flags);

  // Now, we can get going with Vulkan.
  // First step is to create a Vulkan instance.

  this->vulkan = std::make_unique<ren::VulkanInstance>(app_name, this->window);
}

void ren::Engine::run(void) {
  SDL_Event e;
  bool bQuit = false;

  // main loop
  while (!bQuit) {
    // Handle events on queue
    while (SDL_PollEvent(&e) != 0) {
      ImGui_ImplSDL2_ProcessEvent(&e);

      // if the window resizes
      if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
        int width = e.window.data1;
        int height = e.window.data2;
        fmt::print("Window resized to {}x{}\n", width, height);
        // Update the Vulkan swapchain with the new size
        this->vulkan->framebuffer_resized = true;
      }

      // close the window when user alt-f4s or clicks the X button
      if (e.type == SDL_QUIT) {
        bQuit = true;
      } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_SPACE) {}
      }
    }

    // auto start = std::chrono::high_resolution_clock::now();
    vulkan->draw_frame();
    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    // fmt::print("Frame {} took {} ms\n", vulkan->frame_number, duration.count());
  }
  // Before exting, we need to wait for the device to finish all operations.
  vkDeviceWaitIdle(vulkan->device);
}
