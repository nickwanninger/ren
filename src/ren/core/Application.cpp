
#include <SDL_video.h>
#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>

#include <ren/renderer/pipelines/PipelineStateObject.h>
#include <ren/renderer/ShaderCursor.h>
#include <ren/misc/hash.h>
#include <ren/renderer/SubmissionQueue.h>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <backends/imgui_impl_sdl2.h>
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
#include <ren/core/DebugLines.hpp>
#include <ren/renderer/Sampler.h>
#include <ren/misc/resource_usage.h>

#include <ren/renderer/ShaderProgram.h>


#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <ren/core/Systems.h>
#include <ren/assets/MegaMeshBuffer.h>

#include <ren/core/Flag.h>
#include <ren/renderer/ShaderProgram.h>
#include <ren/scripting/imgui_lua_inspector.hpp>

extern "C" {
#include <luajit.h>
#include <lua.h>
#include <lauxlib.h>
}

#include <ren/core/NodeEditorTest.h>

static ren::Flag<int> kMaxFPS("max-fps", 0,
                              "Maximum framerate for the application, 0 = vsync or uncapped.");

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
      if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER)) {
        ren::println("SDL_Init Error: {}", SDL_GetError());
        throw std::runtime_error("Failed to initialize SDL");
      }
    }



    {
      REN_PROFILE_SCOPE("SDL_CreateWindow");
      SDL_WindowFlags window_flags =
          (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                            (kHighDPI.get() ? SDL_WINDOW_ALLOW_HIGHDPI : 0));

      auto windowName = fmt::format("{} - Ren {}", app_name, REN_VERSION);
      this->window =
          SDL_CreateWindow(windowName.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                           window_size.x, window_size.y, window_flags);
      if (!this->window) {
        ren::println("SDL_CreateWindow Error: {}", SDL_GetError());
        throw std::runtime_error("Failed to create SDL window");
      }
    }

    // This initializes the Vulkan instance and all the necessary Vulkan objects.
    // TODO: make this a resource instead of a value on Application.
    this->renderer = make<Renderer>(this->window);

    world.set_threads(6);

    if (kMaxFPS.get() > 0) { world.set_target_fps(kMaxFPS.get()); }

    this->globalEventEntity = world.entity("ren::events");

    // Enable the flecs world rest api
    ren::world().set<flecs::Rest>({});
    ren::world().import <flecs::stats>();


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

    auto scene = ren::world().entity("scene");
    this->sceneLayer = make<SceneLayer>(*this);
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
    // ren::GraphHandle ssao;
    ren::GraphHandle gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth;
    auto &gbp = ren::addGBuffer(G, gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth);
    // ren::addSSAO(G, gbufferDepth, gbufferNormal, ssao);




    static auto program = make<ShaderProgram>("./test");
    ren::PipelineStateObject trianglePSO;
    trianglePSO.debugName = "Test Triangle PSO";
    trianglePSO.program = program;
    trianglePSO.cullMode = ren::CullMode::None;
    trianglePSO.hasVertexBinding = true;
    trianglePSO.fillMode = ren::FillMode::Solid;




    auto computeProgram = make<ShaderProgram>("./compute");

    float renderScaleTemp = 1.0f;

    // TODO: Move me to a logical class, not here as static variables.
    //       Maybe these should live in a WSI abstraction class?
    static bool swapchainNeedsRebuild = false;
    static bool isResizing = false;

    while (this->running) {
      int eventsHandled = 0;
      REN_PROFILE_SCOPE("Frame");

      {
        REN_PROFILE_SCOPE("SDL Poll");
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
          REN_PROFILE_SCOPE("SDL Dispatch");
          eventsHandled++;


          switch (e.type) {
            case SDL_WINDOWEVENT: {
              switch (e.window.event) {
                case SDL_WINDOWEVENT_RESIZED:
                case SDL_WINDOWEVENT_SIZE_CHANGED: {
                  int width = e.window.data1;
                  int height = e.window.data2;
                  swapchainNeedsRebuild = true;
                  isResizing = true;
                  break;
                }

                case SDL_WINDOWEVENT_EXPOSED: {
                  isResizing = false;
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
              break;
            }
          }



          if (ImGui_ImplSDL2_ProcessEvent(&e)) { continue; }
        }

        REN_PROFILE_COUNTER("SDL Events", eventsHandled);
      }

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
      float time =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime)
              .count();
      auto deltaTime =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime)
              .count();
      this->timeSeconds = time;
      lastTime = currentTime;

      if (!running) break;


      renderer->beginFrame();
      auto &frame = ren::getFrameData();

      ren::Camera::get().update(deltaTime);

      framerateCounter.addFrame(deltaTime);



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



      float targetHeight = 480;
      float width = (float)windowWidth;
      float height = (float)windowHeight;
      // targetHeight = height;
      targetHeight = height * renderScaleTemp;
      float scale = targetHeight / height;
      scale *= renderScaleTemp;
      width *= scale;
      height *= scale;

      auto renderSize = glm::uvec2(width, height);
      G.startFrame(renderSize);


      if (ImGui::BeginMainMenuBar()) {
        // 1. Standard menus on the left
        if (ImGui::BeginMenu("File")) {
          if (ImGui::MenuItem("New")) {}
          if (ImGui::MenuItem("Open")) {
            ren::logUI("Open Menu Item", [=](auto &ctx) {
              ImGui::Text("Open menu item clicked!");
              if (ImGui::Button("Close")) { ren::println("Close was pressed!"); }
            });
          }
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) { ImGui::EndMenu(); }

        if (ImGui::BeginMenu("View")) {
          if (ImGui::MenuItem("ImGui Demo")) {
            ren::logUI("ImGui Demo", [](auto &ctx) { ImGui::ShowDemoWindow(&ctx.opened); });
          }
          ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Boop")) {
          u64 frame = vulkan.frame_number;
          ren::logWindow("Boop", fmt::format("boop{}", frame % 6),
                         [=](auto &ctx) { ImGui::Text("Boop menu item clicked!"); });
        }


        if (ImGui::MenuItem("Bop")) {
          int frame = vulkan.frame_number;
          ren::logUI("Bop",
                     [=](auto &ctx) { ImGui::Text("Bop menu item clicked on frame %d", frame); });
        }

        // // 2. Calculate right-alignment
        // // We get the total width, subtract the width of our text, and a small margin

        char buf[64];
        snprintf(buf, sizeof(buf), "%4d FPS", (int)framerateCounter.getAverageFramerate());

        if (ImGui::MenuItem(buf)) {
          //
        }

        // // Write the right-aligned text in the menu bar.
        // float textWidth = ImGui::CalcTextSize(rightText.c_str()).x;
        // float windowWidth = ImGui::GetWindowWidth();
        // ImGui::SetCursorPosX(windowWidth - textWidth - 10.0f);
        // ImGui::Text("%s", rightText.c_str());

        ImGui::EndMainMenuBar();
      }



      ren::inspectLog();

      ImGui::Begin("Compute Shader Inspection");

      if (ImGui::Button("Log Inspector")) {
        ren::logInspection<ShaderProgram>("Programs > Compute Shader Program", computeProgram);
      }
      ImGui::SameLine();

      if (ImGui::Button("Reload")) {
        try {
          computeProgram = make<ShaderProgram>("./compute");
        } catch (const std::exception &e) {
          ren::errln("XXX Failed to reload compute shader: {}", e.what());
        }
      }
      // computeProgram->inspect();

      ImGui::End();


      ImGui::Begin("Debug Temp Settings");
      ImGui::DragFloat("Render Scale", &renderScaleTemp, 0.01f, 0.1f, 1.0f);
      ImGui::Text("FPS: %.1f", framerateCounter.getAverageFramerate());
      ImGui::Text("Frame Time: %.3f ms", framerateCounter.getAverageDeltaTime() * 1000.0f);

      if (ImGui::Button("Save Pipeline Cache")) {
        renderer->getPipelineCache().save("pipeline_cache.bin");
      }
      ImGui::End();

      {
        REN_PROFILE_SCOPE("WorldProgress");
        world.progress(deltaTime);
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
          ren::println("Error running lua update: {}", err.what());
        }
      }

      // world.lookup("scene").scope([&]() { world.progress(deltaTime); });


      try {
        G.runFor(gbufferAlbedo, *renderer);
      } catch (const std::exception &e) {
        ren::println("✗ RenderPassTask execution failed: {}", e.what());
      }

      G.inspect();

      // ren::resource<neko::luainspector>().draw();

      // world.defer_begin();

      auto enc = frame.commandEncoder;
      auto penc = enc->beginRenderPass(*renderer->getDisplayPass(), *frame.renderTarget);
      {
        if (1) {
          auto start = std::chrono::high_resolution_clock::now();
          ren::MeshBuilder b;


          static float p = 0.5f;
          static int segments = 6;
          static int repeatCount = 1;
          if (segments < 3) segments = 3;
          srand(0);



          auto fb = b.beginFace();
          // make a circle with N segments.
          for (int i = 0; i < segments; i++) {
            // compute a random distance
            float distance = 0.1f + (rand() % 1000 / 1000.0f);
            distance *= 0.5f;

            float rad = (float)i / segments * glm::two_pi<float>();
            rad += time * 0.5f;
            // just the point on the unit circle.
            fb.vertex(glm::vec3(cosf(rad), sinf(rad), 0.0f) * distance, glm::vec3(0.0f),
                      glm::vec2(0.0f));
          }
          fb.end();

          auto meshData = b.stampOut();


          static struct {
            float brightness = 1.0f;
            float stride = 0.001f;
            int index = 1;
            int numDraws = 1;
          } pc;

          penc.bindImmediateMesh(meshData->vertices, meshData->indices);
          penc.bindPipeline(trianglePSO);
          pc.numDraws = repeatCount;
          for (int i = 0; i < repeatCount; i++) {
            DrawArguments args;
            args.vertexCount = static_cast<u32>(meshData->indices.size());
            args.instanceCount = 1;

            pc.index = i;
            vkCmdPushConstants(penc.buf(), trianglePSO.program->getPipelineLayout(),
                               VK_SHADER_STAGE_ALL, 0, sizeof(pc), &pc);
            penc.drawIndexed(args);
          }



          auto end = std::chrono::high_resolution_clock::now();

          float allocTime =
              std::chrono::duration<float, std::chrono::nanoseconds::period>(end - start).count();

          ImGui::Begin("New Perf Test");



          ImGui::SeparatorText("Push Constants");
          if (ImGui::DragFloat("Brightness", &pc.brightness, 0.01f, 0.0f, 10.0f)) {
            ren::println("Brightness: {}", pc.brightness);
          }
          ImGui::DragFloat("Stride", &pc.stride, 0.01f, 0.0f, 1.0f);
          ImGui::Separator();


          framerateCounter.inspect();
          ImGui::Text("Allocated VertexBuffer in %f ms", allocTime / 1000.0 / 1000.0);
          // Pick buildMode
          ImGui::DragInt("Segments", &segments, 1.0f, 3, 1024);
          ImGui::DragInt("Repeat Draws", &repeatCount, 1.0f, 1, 1000);
          if (ImGui::Button("Dump Mesh as OBJ")) { meshData->dumpObj(); }

          int width, height;
          SDL_GetWindowSize(ren::Application::get().getWindow(), &width, &height);
          ImGui::Text("Window Size: %d x %d", width, height);
          SDL_Vulkan_GetDrawableSize(ren::Application::get().getWindow(), &width, &height);
          ImGui::Text("Drawable Size: %d x %d", width, height);



          if (ImGui::BeginTable("Vertices Table", 5,
                                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Index");
            ImGui::TableSetupColumn("x");
            ImGui::TableSetupColumn("y");
            ImGui::TableSetupColumn("z");
            ImGui::TableSetupColumn("UV");
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < meshData->vertices.size(); ++i) {
              const auto &vertex = meshData->vertices[i];
              ImGui::TableNextRow();
              ImGui::TableSetColumnIndex(0);
              ImGui::Text("%zu", i);
              ImGui::TableNextColumn();
              ImGui::Text("%f", vertex.pos.x);
              ImGui::TableNextColumn();
              ImGui::Text("%f", vertex.pos.y);
              ImGui::TableNextColumn();
              ImGui::Text("%f", vertex.pos.z);
              ImGui::TableNextColumn();
              ImGui::Text("%f,%f", vertex.texCoord.x, vertex.texCoord.y);
            }

            ImGui::EndTable();
          }
          ImGui::End();
        }


        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), penc.buf());  // Gross leakage.
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
    ImGui_ImplSDL2_InitForVulkan(getWindow());

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

    ImGuiStyle &style = ImGui::GetStyle();
    // ImVec4 *colors = style.Colors;
    ImVec4 *colors = ImGui::GetStyle().Colors;

    auto windowBackground = ImVec4(0.01f, 0.01f, 0.01f, 1.00f);
    auto lighten = [](const ImVec4 &color, float amount) {
      return ImVec4(color.x + amount, color.y + amount, color.z + amount, color.w);
    };

    colors[ImGuiCol_TextDisabled] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_WindowBg] = windowBackground;
    colors[ImGuiCol_ChildBg] = lighten(windowBackground, 0.01f);
    colors[ImGuiCol_Border] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    colors[ImGuiCol_FrameBg] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBg] = windowBackground;
    colors[ImGuiCol_TitleBgActive] = windowBackground;
    colors[ImGuiCol_TitleBgCollapsed] = windowBackground;
    colors[ImGuiCol_MenuBarBg] = windowBackground;
    colors[ImGuiCol_ScrollbarBg] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.67f, 0.67f, 0.67f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.02f, 0.02f, 0.02f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.16f, 0.16f, 0.16f, 0.80f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(1.00f, 1.00f, 1.00f, 0.0f);
    colors[ImGuiCol_TabDimmed] = windowBackground;
    colors[ImGuiCol_TabDimmedSelected] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.98f, 0.99f, 1.00f, 0.09f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = lighten(windowBackground, 0.02f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.03f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.29f, 0.29f, 0.29f, 0.06f);
    colors[ImGuiCol_TreeLines] = ImVec4(0.29f, 0.29f, 0.31f, 0.50f);



    style.TabRounding = 0.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.FontSizeBase = 15.0f;
    style.DockingSeparatorSize = 4.0f;


    auto &am = ren::ensureResource<ren::AssetManager>();
    std::vector<u8> fontBytes;
    if (am.load("fonts/MapleMono-Medium.ttf", fontBytes)) {
      ImFontConfig fontConfig;
      fontConfig.OversampleH = 3;
      fontConfig.OversampleV = 1;
      fontConfig.PixelSnapH = true;
      fontConfig.FontDataOwnedByAtlas = false;
      io.Fonts->AddFontFromMemoryTTF(fontBytes.data(), static_cast<int>(fontBytes.size()),
                                     style.FontSizeBase, &fontConfig);
    } else {
      ren::println("Failed to load font from asset manager!");
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
