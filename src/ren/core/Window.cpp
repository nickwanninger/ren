#include <ren/core/Window.h>
#include <ren/core/Flag.h>
#include <ren/core/Instrumentation.h>
#include <ren/core/Logging.h>
#include <ren/core/ui/EditorUI.h>
#include <ren/renderer/Renderer.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/submission/FrameContext.h>
#include <ren/renderer/submission/SubmissionQueue.h>
#include <SDL3/SDL_vulkan.h>
#include <fmt/core.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_sdl3.h>
#include <imnodes/imnodes.h>

namespace ren {

  static ren::Flag<bool> vsyncFlag("vsync", true, "Enable VSync for the swapchain");


  void Window::initialize(ref<VulkanInstance> vulkan, const WindowDesc &desc) {
    REN_PROFILE_FUNCTION();
    if (m_window != nullptr) {
      throw std::runtime_error("Window is already initialized");
    }
    m_vulkan = std::move(vulkan);

    {
      REN_PROFILE_SCOPE("SDL_CreateWindow");
      auto flags = desc.flags | SDL_WINDOW_VULKAN;
      m_window = SDL_CreateWindow(desc.title.c_str(), desc.size.x, desc.size.y, flags);
      if (m_window == nullptr) {
        throw std::runtime_error(fmt::format("SDL_CreateWindow failed: {}", SDL_GetError()));
      }
    }

    if (!SDL_Vulkan_CreateSurface(m_window, m_vulkan->instance, nullptr, &m_surface)) {
      SDL_DestroyWindow(m_window);
      m_window = nullptr;
      throw std::runtime_error(fmt::format("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError()));
    }


#if __APPLE__
    SDL_Event evt;
    //
    // Work around macos focus issue, possibly caused by "iTerm2"
    //
    while (SDL_PollEvent(&evt) != 0) {
    }
#endif

    //
    // Ensure the window is always in front.
    //
    SDL_RaiseWindow(m_window);
  }


  void Window::initializeUI(void) {
    REN_PROFILE_FUNCTION();
    auto &vulkan = *m_vulkan;

    VkDescriptorPoolSize pool_sizes[] = {
        //
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = 11;  // size of pool_sizes
    pool_info.pPoolSizes = pool_sizes;

    VK_CHECK(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &m_imguiPool));

    // this initializes the core structures of imgui
    ImGui::CreateContext();
    ImNodes::CreateContext();

    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Move windows only from the title bar. Editor viewports submit no item
    // under the mouse (so ImGuizmo can start a drag), and ImGui otherwise
    // moves a window when a drag starts in its empty space.
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui::StyleColorsDark();


    // this initializes imgui for SDL
    ImGui_ImplSDL3_InitForVulkan(m_window);

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vulkan.instance;
    init_info.PhysicalDevice = vulkan.physical_device;
    init_info.Device = vulkan.device;
    init_info.Queue = vulkan.graphicsQueue->getHandle();
    init_info.DescriptorPool = m_imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.RenderPass = Renderer::get().getDisplayPass()->getHandle();

    ImGui_ImplVulkan_Init(&init_info);

    ren::eui::configure();
  }


  Window::~Window() {
    // A subclass's members are already gone here, so anything that holds an
    // ImGui texture (for example a Viewport) has released it.
    if (m_imguiPool != VK_NULL_HANDLE) {
      REN_PROFILE_SCOPE("ImGui shutdown");
      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImNodes::DestroyContext();
      ImGui::DestroyContext();
      vkDestroyDescriptorPool(m_vulkan->device, m_imguiPool, nullptr);
    }

    // The swapchain waits for the device to be idle in its destructor.
    m_swapchain.reset();
    if (m_surface != VK_NULL_HANDLE) {
      vkDestroySurfaceKHR(m_vulkan->instance, m_surface, nullptr);
    }
    if (m_window != nullptr) {
      SDL_DestroyWindow(m_window);
    }
  }


  glm::uvec2 Window::getSize(void) const {
    int width, height;
    SDL_GetWindowSize(m_window, &width, &height);
    return {width, height};
  }


  bool Window::hasFocus(void) const { return SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS; }


  void Window::handleEvent(const SDL_Event &event) {
    // Other SDL windows (for example ImGui viewports) send window events too.
    const bool isWindowEvent = event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST;
    if (!isWindowEvent || event.window.windowID == getID()) {
      switch (event.type) {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
          m_needsRebuild = true;
          m_resizing = true;
          break;

        // If there is a mouse click event, the window should be raised.
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          SDL_RaiseWindow(m_window);

        case SDL_EVENT_WINDOW_EXPOSED:
          m_resizing = false;
          break;

        default:
          break;
      }
    }

    if (m_imguiPool != VK_NULL_HANDLE) {
      REN_PROFILE_SCOPE("ImGui_ImplSDL3_ProcessEvent");
      ImGui_ImplSDL3_ProcessEvent(&event);
    }

    onEvent(event);
  }


  void Window::rebuildSwapchain(void) {
    REN_PROFILE_FUNCTION();
    m_vulkan->waitForIdle();
    m_swapchain.reset();

    auto info = ren::SwapchainCreateInfo{
        .window = m_window,
        .surface = m_surface,
        .enableVSync = vsyncFlag.get(),
    };
    m_swapchain = makeBox<ren::Swapchain>(info);
    m_needsRebuild = false;
  }


  void Window::beginFrame(void) {
    REN_PROFILE_SCOPE("ImGui New Frame");
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
  }


  void Window::render(FrameContext &frame) {
    m_imguiDrawn = false;

    // Save the ImGui stacks, so that an error in the middle of the UI can
    // close the windows that it left open.
    ImGuiErrorRecoveryState imguiState;
    ImGui::ErrorRecoveryStoreState(&imguiState);

    try {
      onRender(frame);
    } catch (const std::exception &error) {
      ren::errln("Frame discarded: {}", error.what());
      if (!m_imguiDrawn) {
        // The error is already reported, so recover without an assert.
        auto &io = ImGui::GetIO();
        bool asserts = io.ConfigErrorRecoveryEnableAssert;
        io.ConfigErrorRecoveryEnableAssert = false;
        ImGui::ErrorRecoveryTryToRecoverState(&imguiState);
        io.ConfigErrorRecoveryEnableAssert = asserts;
        ImGui::EndFrame();
      }
      Renderer::get().discardFrame(*this);
      m_imguiDrawn = true;
    }

    if (!m_imguiDrawn) {
      // Without this, the next ImGui::NewFrame() asserts.
      ren::errln("Window::onRender() did not call drawImGui(). Ending the ImGui frame without drawing it.");
      ImGui::EndFrame();
    }
  }


  void Window::onRender(FrameContext &frame) {
    Renderer::get().prepareGraph();
    Renderer::get().recordGraph();
    auto pass = frame.getMainCommandEncoder()->beginRenderPass(*Renderer::get().getDisplayPass(), getSwapchain().currentTarget());
    drawImGui(pass);
    pass.end();
  }


  void Window::drawImGui(RenderPassEncoder &pass) {
    REN_PROFILE_SCOPE("RenderImGui");
    if (m_imguiDrawn) {
      ren::errln("Window::drawImGui() called more than one time in a frame.");
      return;
    }
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), pass.buf());
    m_imguiDrawn = true;
  }


}  // namespace ren
