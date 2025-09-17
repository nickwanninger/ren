#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_vulkan.h>

#include <ren/layers/ImGuiLayer.h>
#include <ren/core/Instrumentation.h>
#include <ren/renderer/RenderPass.h>
#include <ren/core/Application.h>
#include <ren/types.h>
#include <ren/misc/resource_usage.h>

#include <ImGuizmo/ImGuizmo.h>
#include <ren/core/AutoPlugin.h>
#include <ren/core/Systems.h>
#include <ren/core/FramerateCounter.h>
namespace ren {


  struct ImGuiState {
    FramerateCounter framerateCounter;            // Framerate counter for ImGui
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;  // Descriptor pool for ImGui
  };


  void imgui_plugin(ren::Application &app) {
    app.world.emplace<ImGuiState>();


    REN_PROFILE_SCOPE("ImGuiLayer::initialize");
    auto &vulkan = ren::getVulkan();
    auto &state = app.world.get_mut<ImGuiState>();

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

    VK_CHECK(vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &state.imguiPool));


    // 2: initialize imgui library

    // this initializes the core structures of imgui
    ImGui::CreateContext();

    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  //  | ImGuiConfigFlags_ViewportsEnable;


    // io.Fonts->AddFontFromFileTTF("assets/fonts/Roboto-Medium.ttf", 15);
    // io.Fonts->AddFontFromFileTTF("assets/fonts/FiraCode-Medium.ttf", 14);


    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();




    // this initializes imgui for SDL
    ImGui_ImplSDL2_InitForVulkan(app.getWindow());

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vulkan.instance;
    init_info.PhysicalDevice = vulkan.physical_device;
    init_info.Device = vulkan.device;
    init_info.Queue = vulkan.graphics_queue;
    init_info.DescriptorPool = state.imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = ren::getVulkan().msaaSamples;
    init_info.RenderPass = Renderer::get().getDisplayPass()->getHandle();

    ImGui_ImplVulkan_Init(&init_info);



    ImGuiStyle &style = ImGui::GetStyle();
    // if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    //   style.WindowRounding = 0.0f;
    //   style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    // }


    auto &colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4{0.01f, 0.01f, 0.01f, 0.9f};

    auto border = ImVec4{0.1f, 0.1f, 0.1f, 1.0f};
    auto themeColor = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    auto themeColorHovered = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};

    colors[ImGuiCol_Border] = border;

    // Headers
    colors[ImGuiCol_Header] = border;
    colors[ImGuiCol_HeaderHovered] = border;
    colors[ImGuiCol_HeaderActive] = border;

    // Buttons
    colors[ImGuiCol_Button] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};
    colors[ImGuiCol_ButtonHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_ButtonActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    // Frame BG
    colors[ImGuiCol_FrameBg] = border;
    colors[ImGuiCol_FrameBgHovered] = ImVec4{0.3f, 0.305f, 0.31f, 1.0f};
    colors[ImGuiCol_FrameBgActive] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TabHovered] = ImVec4{0.38f, 0.3805f, 0.381f, 1.0f};
    colors[ImGuiCol_TabActive] = ImVec4{0.28f, 0.2805f, 0.281f, 1.0f};
    colors[ImGuiCol_TabUnfocused] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{0.2f, 0.205f, 0.21f, 1.0f};

    // Title
    colors[ImGuiCol_TitleBg] = border;
    colors[ImGuiCol_TitleBgActive] = border;
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{0.15f, 0.1505f, 0.151f, 1.0f};

    colors[ImGuiCol_ButtonHovered] = ImVec4(1.f, 1.f, 1.f, 1.f);

    // Update controls
    for (auto &style : {
             ImGuiCol_CheckMark,
             ImGuiCol_SliderGrab,
             ImGuiCol_SliderGrabActive,
             ImGuiCol_ResizeGrip,
             ImGuiCol_Button,
         }) {
      colors[style] = themeColor;
    }


    auto sys = ren::system::make_system(flecs::OnLoad, "ren::imgui::Init").run([](flecs::iter &it) {
      REN_PROFILE_SCOPE("ImGui New Frame");
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplSDL2_NewFrame();

      ImGui::NewFrame();
      ImGuizmo::BeginFrame();
      ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());

      int windowWidth, windowHeight;
      SDL_GetWindowSize(ren::Application::get().getWindow(), &windowWidth, &windowHeight);
      ImGuizmo::SetRect(0.0f, 0.0f, windowWidth, windowHeight);


      // Before rendering, lets create a dockspace
      ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                   ImGuiDockNodeFlags_PassthruCentralNode);
    });

    app.atExit([]() {
      REN_PROFILE_SCOPE("ImGuiLayer::shutdown");

      auto &vulkan = ren::getVulkan();
      auto &state = ren::world().get_mut<ImGuiState>();

      vkDestroyDescriptorPool(vulkan.device, state.imguiPool, nullptr);
      state.imguiPool = VK_NULL_HANDLE;

      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplSDL2_Shutdown();
      ImGui::DestroyContext();
    });
  }

  REN_PLUGIN("imgui", imgui_plugin);



  ImGuiLayer::ImGuiLayer(Application &app)
      : Layer(app, "ImGui") {}


  void ImGuiLayer::onAttach(void) {}


  void ImGuiLayer::onDetach(void) {}

  void ImGuiLayer::onEvent(Event &event) {
    REN_PROFILE_FUNCTION();
    auto &io = ImGui::GetIO();
    if (ImGui_ImplSDL2_ProcessEvent(&event.sdlEvent)) { event.handled = true; }
  }



  void ImGuiLayer::onImguiRender(float deltaTime) {
    framerateCounter.addFrame(deltaTime);

    float fps = framerateCounter.getAverageFramerate();
    ImGui::Begin("Debug State");
    ImGui::Text("Delta Time: %.3f ms", deltaTime * 1000.0f);
    ImGui::Text("FPS: %.1f", fps);
    ImGui::Text("RSS: %.2f MB", ren::getCurrentProcessRSS() / (1024.0f * 1024.0f));

    // Display SDL window size and swapchain size.
    auto &app = ren::Application::get();
    auto &vulkan = ren::getVulkan();
    int sdlWidth, sdlHeight;
    SDL_GetWindowSize(app.getWindow(), &sdlWidth, &sdlHeight);
    ImGui::Text("SDL Window Size: %dx%d", sdlWidth, sdlHeight);
    SDL_Vulkan_GetDrawableSize(app.getWindow(), &sdlWidth, &sdlHeight);
    ImGui::Text("SDL Drawable Size: %dx%d", sdlWidth, sdlHeight);
    float dpiScaleX, dpiScaleY;
    SDL_GetDisplayDPI(0, nullptr, &dpiScaleX, &dpiScaleY);
    ImGui::Text("DPI Scale: %.2f, %.2f", dpiScaleX, dpiScaleY);

    auto &R = ren::Renderer::get();
    auto renderExtent = R.getSwapchain().renderExtent;
    auto deviceExtent = R.getSwapchain().deviceExtent;
    ImGui::Text("Swapchain Render Extent: %dx%d", renderExtent.width, renderExtent.height);
    ImGui::Text("Swapchain Device Extent: %dx%d", deviceExtent.width, deviceExtent.height);


    auto &instr = ren::Instrumentor::Get();
    ImGui::Text("Instrument: %zu, %.2f MB", (size_t)instr.profileEvents,
                instr.profileBytes / (1024.0f * 1024.0f));
    ImGui::End();
  }
}  // namespace ren
