
#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>

#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/pipelines/PipelineCache.h>
#include <ren/misc/hash.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_sdl2.h>
#include <ImGuizmo/ImGuizmo.h>

#include <ren/core/Entity.h>
#include <ren/assets/Mesh.h>

#include <ren/renderer/Descriptors.h>
#include <ren/renderer/RenderGraph.h>
#include <ren/renderer/Sampler.h>
#include <ren/misc/resource_usage.h>

#include <ren/renderer/ShaderProgram.h>


#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <ren/core/Systems.h>
#include <ren/core/SceneRenderer.h>
#include <ren/assets/MegaMeshBuffer.h>

#include <ren/scripting/imgui_lua_inspector.hpp>




static ren::Application *g_application = nullptr;
namespace ren {

  Application &Application::get(void) { return *g_application; }
  Application::Application(const std::string &app_name, glm::uvec2 window_size) {
    g_application = this;
    // Initialize the SDL window
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) {
      fmt::println("SDL_Init Error: {}", SDL_GetError());
      throw std::runtime_error("Failed to initialize SDL");
    }


    SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    this->window =
        SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         window_size.x, window_size.y, window_flags);
    if (!this->window) {
      fmt::println("SDL_CreateWindow Error: {}", SDL_GetError());
      throw std::runtime_error("Failed to create SDL window");
    }

    // This initializes the Vulkan instance and all the necessary Vulkan objects.
    // TODO: make this a resource instead of a value on Application.
    this->renderer = makeRef<Renderer>(this->window);

    // world.set_threads(6);

    this->globalEventEntity = world.entity("ren::events");

    // Enable the flecs world rest api
    ren::world().set<flecs::Rest>({});
    ren::world().import <flecs::stats>();

    ren::initPhases(ren::world());


    // Configure Lua by loading the standard libraries.
    luaL_openlibs(lua.lua_state());

    // Create an asset manager. This part of the heirarchy will hold loaded assets.
    auto &am = ren::ensureResource<ren::AssetManager>();
    am.addEmbeddedSource();
    am.addFilesystemSource("assets/");

    // Configure the lua interpreter to pull from the asset manager.
    am.configureLua(lua);

    // Ensure we have a megamesh buffer. (TODO: move this somewhere non-global.)
    ren::ensureResource<ren::MegaMeshBuffer>();


    sol::table ren_components = lua.create_table();

    lua["package"]["preload"]["ren_components"] = [ren_components](sol::this_state s) {
      sol::state_view L(s);
      sol::table mod = L.create_table();
      mod.set_function("test", [](const sol::object &a) {
        if (a.is<ecs_entity_t>()) {
          ecs_entity_t *ptr = a.as<ecs_entity_t *>();
          fmt::println("Hello from ren_components.test({})", (void *)ptr);
          return 0;
        }
        fmt::println("Hello from ren_components.test(?)");
        return 0;
      });
      return mod;
    };


    world.emplace<neko::luainspector>(ren::lua());


    auto scene = ren::world().entity("scene");


    this->sceneLayer = makeRef<SceneLayer>(*this);
    this->layerStack.pushLayer(sceneLayer);

    // Find and open first PS5 controller
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
      if (SDL_IsGameController(i)) {
        SDL_Joystick *joystick = SDL_JoystickOpen(i);
        if (SDL_JoystickGetVendor(joystick) == 0x054C &&
            SDL_JoystickGetProduct(joystick) == 0x0CE6) {
          sceneLayer->camera.controller = SDL_GameControllerOpen(i);
          SDL_JoystickClose(joystick);
          break;
        }
        SDL_JoystickClose(joystick);
      }
    }


    initImGui();


    ren::AutoPlugin::registerPlugins(*this);


    // This should kick it off.
    auto bootstrap_result = lua.do_string("require 'ren.bootstrap'");
    if (bootstrap_result.status() != sol::call_status::ok) {
      throw std::runtime_error(
          fmt::format("Failed to run ren.bootstrap: {}", bootstrap_result.get<std::string>()));
    }
    auto result = lua.do_string("require 'init'");
    if (result.status() != sol::call_status::ok) {
      throw std::runtime_error(
          fmt::format("Failed to load init.lua: {}", result.get<std::string>()));
    }
  }

  Application::~Application() {
    REN_PROFILE_FUNCTION();
    this->renderer->waitForIdle();
    // Not quite sure when to tear down lua yet.

    // Call exit callbacks
    for (auto &func : exitCallbacks)
      func();

    // Clear the layer stack
    this->layerStack.clear();

    this->sceneLayer.reset();

    {
      REN_PROFILE_SCOPE("ImGuiLayer::shutdown");

      auto &vulkan = ren::getVulkan();
      auto &state = ren::resource<ren::Application::ImGuiState>();

      vkDestroyDescriptorPool(vulkan.device, state.imguiPool, nullptr);
      state.imguiPool = VK_NULL_HANDLE;

      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplSDL2_Shutdown();
      ImGui::DestroyContext();
    }

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


    SceneRenderer sceneRenderer(*this->renderer);


    ren::PipelineCache pipelineCache;
    auto &vulkan = ren::getVulkan();




    int pixelScale = 3;
    float fov = 90.0f;


    // now make a render target for the gbuffer.
    float renderAspect = 0.0f;
    bool targetValid = false;
    ref<RenderTarget> gbufferTarget = nullptr;



    ren::PipelineStateObject blitPSO;
    blitPSO.debugName = "GBuffer Blit PSO";
    blitPSO.program = makeRef<ShaderProgram>("shaders/display");
    blitPSO.cullMode = ren::CullMode::None;
    blitPSO.hasVertexBinding = false;



    float modelScale = 1.0f;
    glm::vec3 modelRotation(0.0f);
    glm::vec3 modelPosition(0.0f);

    FramerateCounter framerateCounter;

    while (this->running) {
      int eventsHandled = 0;
      REN_PROFILE_SCOPE("Frame");

      {
        bool windowResized = false;
        REN_PROFILE_SCOPE("SDL Poll");
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
          REN_PROFILE_SCOPE("SDL Dispatch");
          eventsHandled++;

          if (e.type == SDL_WINDOWEVENT_RESIZED) { fmt::println("resize event"); }
          // close the window when user alt-f4s or clicks the X button
          if (e.type == SDL_QUIT) {
            this->running = false;
            break;
          }

          if (ImGui_ImplSDL2_ProcessEvent(&e)) { continue; }

          Event renEvent(e);
          layerStack.dispatchEvent(renEvent);
        }

        REN_PROFILE_COUNTER("SDL Events", eventsHandled);
      }

      // handle sdl 0 size window
      int windowWidth, windowHeight;
      SDL_GetWindowSize(getWindow(), &windowWidth, &windowHeight);
      if (windowWidth == 0 || windowHeight == 0) {
        SDL_Delay(100);
        continue;
      }


      auto currentTime = std::chrono::high_resolution_clock::now();
      float time =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime)
              .count();
      auto deltaTime =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime)
              .count();
      TracyPlot("Frame Time", deltaTime);
      TracyPlot("FPS", 1.0f / deltaTime);
      this->timeSeconds = time;
      lastTime = currentTime;


      if (!running) break;

      // Get a frame from the swapchain.

      renderer->beginFrame();
      auto &frame = ren::getFrameData();

      auto frameStats = frame.perf.nextFrame(frame.commandBuffer);

      framerateCounter.addFrame(deltaTime);

      {
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
      }


      // Update lua globals
      lua.globals()["time"] = time;
      lua.globals()["delta_time"] = deltaTime;
      lua.globals()["fps"] = framerateCounter.getAverageFramerate();
      lua.globals()["frame"] = vulkan.frame_number;
      // Call the lua update function if it exists
      sol::protected_function lua_update = lua["update"];
      if (lua_update.valid()) {
        sol::protected_function_result result = lua_update(deltaTime);
        if (!result.valid()) {
          sol::error err = result;
          fmt::println("Error running lua update: {}", err.what());
        }
      }

      {
        REN_PROFILE_SCOPE("Tick");
        world.lookup("scene").scope([&]() { world.progress(deltaTime); });
      }


      ren::resource<neko::luainspector>().draw();

      world.defer_begin();
      auto gbufferTarget = sceneRenderer.render(sceneLayer->scene, sceneLayer->camera);

      renderer->withPass(*renderer->getDisplayPass(), *frame.renderTarget, [&]() {
        auto cmd = ren::getFrameData().commandBuffer;

        if (true and gbufferTarget) {
          REN_PROFILE_SCOPE("Blit GBuffer");
          // Blit the gbuffer to the screen temporarily.

          frame.perf.begin(cmd, "Blit GBuffer");
          renderer->bind(blitPSO);

          // begin binding set zero, which is the gbuffer textures.
          auto textureBinder = renderer->startBinding(0);
          auto &attachments = gbufferTarget->getAttachments();
          for (auto &attachment : attachments) {
            if (attachment.name == "outColor") {
              textureBinder.bind("albedo", *attachment.texture);
            }
          }
          textureBinder.apply();

          vkCmdDraw(cmd, 3, 1, 0, 0);
          frame.perf.end(cmd, "Blit GBuffer");
        }




        {
          frame.perf.begin(cmd, "ImGui");
          REN_PROFILE_SCOPE("ImGui Render Draw Data");
          ImGui::Render();
          ImGui::UpdatePlatformWindows();
          ImGui::RenderPlatformWindowsDefault();
          // Gross leakage.
          ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ren::getFrameData().commandBuffer);
          frame.perf.end(cmd, "ImGui");
        }
      });


      world.defer_end();
      renderer->endFrame();



      // Update the layers.
      layerStack.onUpdate(deltaTime);
    }
    renderer->waitForIdle();
  }


  void Application::initImGui() {
    auto &vulkan = ren::getVulkan();
    auto &state = ren::ensureResource<ren::Application::ImGuiState>();

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
    ImGui_ImplSDL2_InitForVulkan(getWindow());

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


    // Table row alternating colors for less high contrast
    colors[ImGuiCol_TableRowBg] = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};        // Transparent
    colors[ImGuiCol_TableRowBgAlt] = ImVec4{0.08f, 0.08f, 0.08f, 0.1f};  // Subtle alternating row

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


    // auto sys = ren::system::onUpdate("ren::Application::RenderStats").run([](flecs::iter &it) {
    //   auto &state = ren::resource<ren::Application::ImGuiState>();
    //   float deltaTime = ren::deltaTime();
    //   state.framerateCounter.addFrame(deltaTime);

    //   float fps = state.framerateCounter.getAverageFramerate();
    //   ImGui::Begin("Debug State");
    //   ImGui::Text("Delta Time: %.3f ms", deltaTime * 1000.0f);
    //   ImGui::Text("FPS: %.1f", fps);
    //   ImGui::Text("RSS: %.2f MB", ren::getCurrentProcessRSS() / (1024.0f * 1024.0f));

    //   // Display SDL window size and swapchain size.
    //   auto &app = ren::Application::get();
    //   auto &vulkan = ren::getVulkan();
    //   int sdlWidth, sdlHeight;
    //   SDL_GetWindowSize(app.getWindow(), &sdlWidth, &sdlHeight);
    //   ImGui::Text("SDL Window Size: %dx%d", sdlWidth, sdlHeight);
    //   SDL_Vulkan_GetDrawableSize(app.getWindow(), &sdlWidth, &sdlHeight);
    //   ImGui::Text("SDL Drawable Size: %dx%d", sdlWidth, sdlHeight);
    //   float dpiScaleX, dpiScaleY;
    //   SDL_GetDisplayDPI(0, nullptr, &dpiScaleX, &dpiScaleY);
    //   ImGui::Text("DPI Scale: %.2f, %.2f", dpiScaleX, dpiScaleY);

    //   auto &R = ren::Renderer::get();
    //   auto renderExtent = R.getSwapchain().renderExtent;
    //   auto deviceExtent = R.getSwapchain().deviceExtent;
    //   ImGui::Text("Swapchain Render Extent: %dx%d", renderExtent.width, renderExtent.height);
    //   ImGui::Text("Swapchain Device Extent: %dx%d", deviceExtent.width, deviceExtent.height);


    //   auto &instr = ren::Instrumentor::Get();
    //   ImGui::Text("Instrument: %zu, %.2f MB", (size_t)instr.profileEvents,
    //               instr.profileBytes / (1024.0f * 1024.0f));
    //   ImGui::End();
    // });
  }



}  // namespace ren



extern "C" {
// LUA API (FFI)
ecs_world_t *__ren_get_world(void) { return ren::world().c_ptr(); }

ecs_entity_t __ren_register_component(const char *name, size_t size, size_t alignment,
                                      const char *desc) {
  auto &world = ren::world();

  auto entity = world.entity(name);
  ecs_entity_t id = entity.id();

  ecs_component_desc_t componentDesc = {};
  componentDesc.entity = id;
  componentDesc.type.size = size;
  componentDesc.type.alignment = alignment;

  id = ecs_component_init(world, &componentDesc);

  ecs_meta_from_desc(world, id, EcsStructType, desc);
  // fmt::println("Registering component '{}' size={} align={} desc={}", name, size, alignment,
  // desc);
  return id;
}


// Register a component that is just a sol::value
ecs_entity_t __ren_register_lua_component(const char *name) {
  auto &world = ren::world();

  auto entity = world.entity(name);
  ecs_entity_t id = entity.id();

  ecs_component_desc_t componentDesc = {};
  componentDesc.entity = id;
  componentDesc.type.size = sizeof(sol::object);
  componentDesc.type.alignment = alignof(sol::object);

  id = ecs_component_init(world, &componentDesc);

  return id;
}
}