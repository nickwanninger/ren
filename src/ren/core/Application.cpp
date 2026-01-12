
#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>

#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/misc/hash.h>
#include <ren/renderer/submission/SubmissionQueue.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_sdl3.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <imnodes/imnodes.h>

#include <ren/core/Entity.h>
#include <ren/assets/Mesh.h>
#include <ren/assets/MeshBuilder.h>

#include <ren/renderer/Buffer.h>
#include <ren/renderer/Descriptors.h>
#include <ren/renderer/graph/RenderGraph.h>
#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/tasks/DepthPrepassTask.h>
#include <ren/renderer/graph/tasks/SSAOTask.h>
#include <ren/renderer/graph/tasks/ShadowMapTask.h>
#include <ren/renderer/graph/tasks/GBufferTask.h>
#include <ren/renderer/Sampler.h>
#include <ren/renderer/shader/ShaderProgram.h>

#include <ren/core/ui/EditorUI.h>
#include <ren/core/Watcher.h>
#include <ren/core/DebugLines.hpp>
#include <ren/core/Color.h>
#include <ren/misc/resource_usage.h>



#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <ren/core/Systems.h>
#include <ren/assets/MegaMeshBuffer.h>

#include <ren/core/Flag.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/scripting/imgui_lua_inspector.hpp>

#include <ren/core/ThreadPool.h>
#include <ren/core/Math.h>

extern "C" {
#include <luajit.h>
#include <lua.h>
#include <lauxlib.h>
}

#include <ren/core/NodeEditorTest.h>

static ren::Flag<int> kMaxFPS("max-fps", 0, "Maximum framerate for the application, 0 = vsync or uncapped.");

static ren::Flag<bool> kHighDPI("high-dpi", true, "Enable high DPI support.");

static ren::Application *g_application = nullptr;
namespace ren {

  Application &Application::get(void) { return *g_application; }
  Application::Application(const std::string &app_name, glm::uvec2 window_size) {
    REN_PROFILE_SCOPE("BringupApplication");
    g_application = this;
    // Initialize the SDL window

    {
      REN_PROFILE_SCOPE("SDL_Init");

      auto initFlags = SDL_INIT_VIDEO;
      initFlags |= SDL_INIT_CAMERA;

      if (SDL_Init(initFlags) == false) {
        ren::println("SDL_Init Error: {}", SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
      }
    }


    {
      REN_PROFILE_SCOPE("SDL_CreateWindow");

      auto flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
      if (kHighDPI.get()) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
      }

      auto windowName = fmt::format("{} - Ren {}", app_name, REN_VERSION);
      this->window = SDL_CreateWindow(windowName.c_str(), window_size.x, window_size.y, flags);
      if (!this->window) {
        ren::println("SDL_CreateWindow Error: {}", SDL_GetError());
        throw std::runtime_error("Failed to create SDL window");
      }
    }


    // This initializes the Vulkan instance and all the necessary Vulkan objects.
    // TODO: make this a resource instead of a value on Application.
    this->renderer = make<Renderer>(this->window);

    world.set_threads(6);

    if (kMaxFPS.get() > 0) {
      world.set_target_fps(kMaxFPS.get());
    }

    this->globalEventEntity = world.entity("ren::events");

    // Enable the flecs world rest api
    ren::world().set<flecs::Rest>({});
    ren::world().import <flecs::stats>();
    ren::world().import <flecs::timer>();


    ren::initPhases(ren::world());

    // Configure Lua by loading the standard libraries.
    luaL_openlibs(lua.lua_state());

    auto *L = ren::lua();
    if (luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON) == 0) {
      ren::println("Warning: failed to enable LuaJIT JIT engine");
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

    initImGui();


    ren::AutoPlugin::registerPlugins(*this);


    // This should kick it off.
    auto bootstrap_result = lua.do_string("require 'ren.bootstrap'");
    if (bootstrap_result.status() != sol::call_status::ok) {
      throw std::runtime_error(fmt::format("Failed to run ren.bootstrap: {}", bootstrap_result.get<std::string>()));
    }
    auto result = lua.do_string("require 'init'");
    if (result.status() != sol::call_status::ok) {
      throw std::runtime_error(fmt::format("Failed to load init.lua: {}", result.get<std::string>()));
    }
  }

  Application::~Application() {
    REN_PROFILE_FUNCTION();
    this->renderer->waitForIdle();
    // Not quite sure when to tear down lua yet.

    // Call exit callbacks
    for (auto &func : exitCallbacks) {
      func();
    }

    {
      REN_PROFILE_SCOPE("ImGuiLayer::shutdown");

      auto &vulkan = ren::getVulkan();
      auto &state = ren::resource<ren::Application::ImGuiState>();

      vkDestroyDescriptorPool(vulkan.device, state.imguiPool, nullptr);
      state.imguiPool = VK_NULL_HANDLE;

      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImNodes::DestroyContext();
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


    auto &vulkan = ren::getVulkan();
    FramerateCounter framerateCounter;



    ren::RenderGraph G;
    ren::GraphHandle nullHandleOut;
    ren::GraphHandle ssao;
    ren::GraphHandle gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth;
    auto &gbp = ren::addGBuffer(G, gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth);
    ren::addSSAO(G, gbufferDepth, gbufferNormal, ssao);




    static auto program = make<ShaderProgram>("./test");
    ren::PipelineStateObject trianglePSO;
    trianglePSO.debugName = "Test Triangle PSO";
    trianglePSO.program = program;
    trianglePSO.cullMode = ren::CullMode::None;
    trianglePSO.hasVertexBinding = true;
    trianglePSO.fillMode = ren::FillMode::Solid;


    float renderScaleTemp = 1.0f;

    // TODO: Move me to a logical class, not here as static variables.
    //       Maybe these should live in a WSI abstraction class?
    static bool swapchainNeedsRebuild = false;
    static bool isResizing = false;


    SDL_RaiseWindow(this->window);

    while (this->running) {
      int eventsHandled = 0;

      {
        // REN_PROFILE_SCOPE("SDL Poll");
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
          REN_PROFILE_SCOPE("SDL Dispatch");
          eventsHandled++;


          switch (e.type) {
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
              int width = e.window.data1;
              int height = e.window.data2;
              swapchainNeedsRebuild = true;
              isResizing = true;
              break;
            }

            case SDL_EVENT_WINDOW_EXPOSED: {
              isResizing = false;
              break;
            }


            case SDL_EVENT_QUIT: {
              this->running = false;
              break;
            }
            default: {
              break;
            }
          }



          if (ImGui_ImplSDL3_ProcessEvent(&e)) {
            continue;
          }
        }

        REN_PROFILE_COUNTER("SDL Events", eventsHandled);
      }


      REN_PROFILE_SCOPE("Frame");

      // Handle swapchain rebuild if needed, and the state machine allows.
      if (swapchainNeedsRebuild && !isResizing) {
        REN_PROFILE_SCOPE("Rebuild Swapchain");
        renderer->rebuildSwapchain();
        swapchainNeedsRebuild = false;
        continue;
      }

      // handle sdl 0 size window
      int windowWidth, windowHeight;
      SDL_GetWindowSize(getWindow(), &windowWidth, &windowHeight);
      if (windowWidth == 0 || windowHeight == 0) {
        ren::println("Window minimized, skipping frame");
        SDL_Delay(100);
        continue;
      }


      auto currentTime = std::chrono::high_resolution_clock::now();
      float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
      auto deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
      this->timeSeconds = time;
      lastTime = currentTime;

      if (!running) {
        break;
      }


      renderer->beginFrame();
      auto &frame = ren::getFrameUnit();

      ren::Camera::get().update(deltaTime);

      framerateCounter.addFrame(deltaTime);



      {
        REN_PROFILE_SCOPE("ImGui New Frame");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList(ImGui::GetMainViewport()));

        int windowWidth, windowHeight;
        SDL_GetWindowSize(ren::Application::get().getWindow(), &windowWidth, &windowHeight);
        ImGuizmo::SetRect(0.0f, 0.0f, windowWidth, windowHeight);


        // Before rendering, lets create a dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
      }



      // float width = (float)windowWidth;
      // float height = (float)windowHeight;
      // // targetHeight = height;
      // float targetHeight = height * renderScaleTemp;
      // float scale = targetHeight / height;
      // scale *= renderScaleTemp;
      // width *= scale;
      // height *= scale;

      // auto renderSize = glm::uvec2(width, height);
      // G.startFrame(renderSize);


      if (ImGui::BeginMainMenuBar()) {

        if (ImGui::BeginMenu("View")) {
          if (ImGui::MenuItem("ImGui Demo")) {
            ren::logUI("ImGui Demo", [](auto &ctx) { ImGui::ShowDemoWindow(&ctx.opened); });
          }
          ImGui::EndMenu();
        }


        char buf[64];
        snprintf(buf, sizeof(buf), "%4d FPS", (int)framerateCounter.getAverageFramerate());
        if (ImGui::MenuItem(buf)) {
          //
        }

        ImGui::EndMainMenuBar();
      }

      /*
      ImGui::Begin("VRAM");
      {
        auto start = std::chrono::high_resolution_clock::now();

        VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
        vmaGetHeapBudgets(getVulkan().allocator, budgets);

        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(getVulkan().physical_device, &memProps);

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

        ImGui::Text("Query took %lluns", duration);
        // 3. Iterate through memory heaps to find VRAM
        for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
          u64 budget = budgets[i].budget;
          u64 usage = budgets[i].usage;
          u64 total = memProps.memoryHeaps[i].size;

          float usagePercent = (float)usage / (float)budget * 100.0f;

          ImGui::Text("VRAM Heap %u: %llu MB used / %llu MB budget / %llu MB total", i, usage / (1024 * 1024), budget / (1024 * 1024),
                      total / (1024 * 1024));

          // display a progress bar
          ImGui::ProgressBar(usagePercent / 100.0f, ImVec2(-1.0f, 0.0f), fmt::format("{:.2f} %", usagePercent).c_str());
        }
      }
      ImGui::End();
      */



      // ren::inspectLog();

      if (1) {
        REN_PROFILE_SCOPE("WorldProgress");
        world.progress(deltaTime);

        REN_PROFILE_SCOPE("LuaProgress");
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
            ren::println("Error running lua update: {}", err.what());
          }
        }
      }




      float width = (float)windowWidth;
      float height = (float)windowHeight;
      // targetHeight = height;
      float targetHeight = height * renderScaleTemp;
      float scale = targetHeight / height;
      scale *= renderScaleTemp;
      width *= scale;
      height *= scale;
      auto renderSize = glm::uvec2(width, height);
      G.startFrame(renderSize);

      try {
        G.runFor(ssao, *renderer);
      } catch (const std::exception &e) {
        ren::println("✗ RenderPassTask execution failed: {}", e.what());
      }

      G.inspect();



      auto enc = frame.getMainCommandEncoder();
      auto penc = enc->beginRenderPass(*renderer->getDisplayPass(), *frame.renderTarget);
      {
        REN_PROFILE_SCOPE("FullScreenPass");
        if (1) {
          REN_PROFILE_SCOPE("RenderBackgroundTriangle");
          auto start = std::chrono::high_resolution_clock::now();
          ren::MeshBuilder b;

          auto fb = b.beginFace();

          // Add triangles to make a full screen quad
          fb.vertex(glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(0.0f, 0.0f));
          fb.vertex(glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(1.0f, 0.0f));
          fb.vertex(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(1.0f, 1.0f));
          fb.vertex(glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(0.0f, 1.0f));

          fb.end();

          auto meshData = b.stampOut();


          static struct {
            float brightness = 1.0f;
            float time = 0.0f;
            float stride = 0.001f;
            int index = 1;
            int numDraws = 1;
          } pc;

          penc.bindImmediateMesh(meshData->vertices, meshData->indices);
          penc.bindPipeline(trianglePSO);
          pc.numDraws = 0;
          pc.time = time;
          DrawArguments args;
          args.vertexCount = static_cast<u32>(meshData->indices.size());
          args.instanceCount = 1;

          pc.index = 0;
          vkCmdPushConstants(penc.buf(), trianglePSO.program->getPipelineLayout(), VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
          penc.drawIndexed(args);

          auto end = std::chrono::high_resolution_clock::now();

          float allocTime = std::chrono::duration<float, std::chrono::nanoseconds::period>(end - start).count();
        }


        {
          REN_PROFILE_SCOPE("RenderImGui");
          ImGui::Render();
          ImGui::UpdatePlatformWindows();
          ImGui::RenderPlatformWindowsDefault();
          ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), penc.buf());  // Gross leakage.
        }
      }

      penc.end();

      // world.defer_end();
      renderer->endFrame();
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
    ImNodes::CreateContext();

    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;  //  | ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();


    // this initializes imgui for SDL
    ImGui_ImplSDL3_InitForVulkan(getWindow());

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vulkan.instance;
    init_info.PhysicalDevice = vulkan.physical_device;
    init_info.Device = vulkan.device;
    init_info.Queue = vulkan.graphicsQueue->getHandle();
    init_info.DescriptorPool = state.imguiPool;
    init_info.MinImageCount = 3;
    init_info.ImageCount = 3;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.RenderPass = Renderer::get().getDisplayPass()->getHandle();

    ImGui_ImplVulkan_Init(&init_info);

    ren::eui::configure();
  }




}  // namespace ren



extern "C" {
// LUA API (FFI)
ecs_world_t *__ren_get_world(void) { return ren::world().c_ptr(); }

ecs_entity_t __ren_register_component(const char *name, size_t size, size_t alignment, const char *desc) {
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
