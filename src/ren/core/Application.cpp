
#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>

#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/pipelines/PipelineCache.h>
#include <ren/misc/hash.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_sdl2.h>
#include <ImGuizmo/ImGuizmo.h>

#include <ren/core/Entity.h>
#include <ren/assets/Mesh.h>

#include <ren/renderer/Descriptors.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/tasks/DepthPrepassTask.h>
#include <ren/renderer/graph/tasks/SSAOTask.h>
#include <ren/renderer/graph/tasks/ShadowMapTask.h>
#include <ren/renderer/graph/tasks/GBufferTask.h>
#include <ren/core/DebugLines.hpp>
#include <ren/renderer/Sampler.h>
#include <ren/misc/resource_usage.h>

#include <ren/renderer/ShaderProgram.h>


#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <ren/core/Systems.h>
#include <ren/core/SceneRenderer.h>
#include <ren/assets/MegaMeshBuffer.h>

#include <ren/renderer/ShaderProgram.h>
#include <ren/scripting/imgui_lua_inspector.hpp>

extern "C" {
#include <luajit.h>
#include <lua.h>
#include <lauxlib.h>
}

// TEMP: Test RenderPassTask implementation (will remove later)
namespace {
  class TestPass : public ren::RenderPassTask {
   public:
    ren::GraphHandle colorOut;

    ren::PipelineStateObject pso;

    TestPass(ren::RenderGraph &graph)
        : ren::RenderPassTask(graph) {
      colorOut = addColorAttachment(
          "test_color", {.width = 512, .height = 512, .format = VK_FORMAT_R8G8B8A8_SRGB});

      pso.program = ren::ShaderProgram::makeFullScreenProgram("shaders/debug/uv.frag");
      pso.cullMode = ren::CullMode::None;
      pso.depthTest = false;
      pso.depthWrite = false;
      pso.hasVertexBinding = false;
    }

    void run(ren::GraphRunContext &ctx) override {
      ctx.renderer.bind(pso);
      vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
    }

    void inspect(void) override { pso.program->inspect(); }
  };
}  // namespace



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


    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    auto windowName = fmt::format("{} -- Ren {} - {}", app_name, REN_GIT_REVISION, REN_BUILD_DATE);
    this->window =
        SDL_CreateWindow(windowName.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
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

    LUAJIT_VERSION_SYM();  // ensure luajit is linked
    // Configure Lua by loading the standard libraries.
    luaL_openlibs(lua.lua_state());

    auto *L = ren::lua();
    if (luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON) == 0) {
      fmt::println("Warning: failed to enable LuaJIT JIT engine");
    }


    // Create an asset manager. This part of the heirarchy will hold loaded assets.
    auto &am = ren::ensureResource<ren::AssetManager>();
    am.addEmbeddedSource();
    am.addFilesystemSource("assets/");

    // Configure the lua interpreter to pull from the asset manager.
    am.configureLua(lua);

    // Ensure we have a megamesh buffer. (TODO: move this somewhere non-global.)
    ren::ensureResource<ren::MegaMeshBuffer>();

    world.emplace<neko::luainspector>(ren::lua());


    auto scene = ren::world().entity("scene");


    this->sceneLayer = makeRef<SceneLayer>(*this);
    this->layerStack.pushLayer(sceneLayer);

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
    ren::RenderGraph G;


    ren::GraphHandle nullHandleOut;

    ren::GraphHandle ssao;
    ren::GraphHandle gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth;
    auto &gbp = ren::addGBuffer(G, gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth);

    // ren::GraphHandle shadowMap;                          // TEMP
    // ren::addShadowMap(G, 64, shadowMap);      // TEMP
    // gbp.read(shadowMap, ren::GraphAccess::DepthTarget);  // FORCE, TEMP

    ren::addSSAO(G, gbufferDepth, gbufferNormal, ssao);




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


          switch (e.type) {
            case SDL_DROPFILE: {
              // File dropped
              char *dropped_filedir = e.drop.file;
              // Process the file path
              printf("File dropped: %s\n", dropped_filedir);
              SDL_free(dropped_filedir);  // Must free this!
              break;
            }


            case SDL_DROPBEGIN: {
              fmt::println("Drop Begin");
              // Drop operation beginning (SDL 2.0.5+)
              break;
            }

            case SDL_DROPCOMPLETE: {
              fmt::println("Drop Complete");
              // Drop operation complete (SDL 2.0.5+)
              break;
            }

            case SDL_WINDOWEVENT: {
              switch (e.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED: {
                  // fmt::println("!!!Window resized to {}x{}", e.window.data1, e.window.data2);
                  windowResized = true;
                  break;
                }
              }
              break;
            }


            case SDL_QUIT: {
              this->running = false;
              break;
            }
            default: {
              // fmt::println("Unhandled SDL Event Type: {}\n", e.type);
              break;
            }
          }



          // if (e.type == SDL_WINDOWEVENT_RESIZED) { fmt::println("resize event"); }
          // // close the window when user alt-f4s or clicks the X button
          // if (e.type == SDL_QUIT) {
          //   this->running = false;
          //   break;
          // }

          if (ImGui_ImplSDL2_ProcessEvent(&e)) { continue; }

          Event renEvent(e);
          layerStack.dispatchEvent(renEvent);
        }

        if (windowResized) fmt::println("Window resized");

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




      float width = frame.deviceImage->getWidth();
      float height = frame.deviceImage->getHeight();


      float targetHeight = 480;
      targetHeight = height;
      float scale = targetHeight / height;
      width *= scale;
      height *= scale;


      auto renderSize = glm::uvec2(width, height);



      G.startFrame(renderSize);




      {
        REN_PROFILE_SCOPE("ImGui New Frame");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList(ImGui::GetMainViewport()));

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


      // TEMP: Execute test RenderPassTask for validation
      try {
        G.runFor(ssao, *renderer);
      } catch (const std::exception &e) {
        fmt::println("✗ RenderPassTask execution failed: {}", e.what());
      }

      G.inspect();

      ren::resource<neko::luainspector>().draw();

      world.defer_begin();
      // auto gbufferTarget = sceneRenderer.render(sceneLayer->scene, sceneLayer->camera);

      renderer->withPass(*renderer->getDisplayPass(), *frame.renderTarget, [&]() {
        static struct BlitConfiguration {
          float exposure = 1.0f;
          float ditherDivide = 256.0f;
        } blitConfig;

        ImGui::Begin("Display Settings");
        ImGui::DragFloat("Exposure", &blitConfig.exposure, 0.01f, 0.0f, 100.0f);
        ImGui::DragFloat("Dither Divide", &blitConfig.ditherDivide, 1.0f, 1.0f, 1024.0f, "%.0f");
        ImGui::End();

        static ren::UniformBufferSet<BlitConfiguration> blitConfigBuffer(1);
        blitConfigBuffer.update(&blitConfig, 1, 0);

        auto cmd = ren::getFrameData().commandBuffer;

        {
          REN_PROFILE_SCOPE("Blit GBuffer");
          // Blit the gbuffer to the screen temporarily.

          frame.perf.begin(cmd, "Blit GBuffer");
          renderer->bind(blitPSO);

          // begin binding set zero, which is the gbuffer textures.
          auto blitBinder = renderer->startBinding(0);
          blitBinder.bind("config", blitConfigBuffer.currentAsBuffer());

          blitBinder.bind("albedo", *G.getImage(gbufferAlbedo), VK_FILTER_NEAREST);
          blitBinder.bind("ssao", *G.getImage(ssao), VK_FILTER_LINEAR);
          blitBinder.apply();

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
  }



}  // namespace ren



extern "C" {
// LUA API (FFI)
ecs_world_t *__ren_get_world(void) { return ren::world().c_ptr(); }

ecs_entity_t __ren_register_component(const char *name, size_t size, size_t alignment,
                                      const char *desc) {
  auto &world = ren::world();

  auto entity = world.entity(name, ".", ".");
  ecs_entity_t id = entity.id();

  ecs_component_desc_t componentDesc = {};
  componentDesc.entity = id;
  componentDesc.type.size = size;
  componentDesc.type.alignment = alignment;

  id = ecs_component_init(world, &componentDesc);

  ecs_meta_from_desc(world, id, EcsStructType, desc);
  return id;
}
}
