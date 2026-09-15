
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
#include <ren/core/Math.h>

#include <chrono>
#include <thread>

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

    this->globalEventEntity = world.entity("ren::events");

    // Enable the flecs world rest api
    ren::world().set<flecs::Rest>({});
    ren::world().import<flecs::stats>();
    ren::world().import<flecs::timer>();


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

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    {
      REN_PROFILE_SCOPE("ImGuiLayer::shutdown");

      auto &vulkan = ren::getVulkan();
      auto &state = ren::resource<ren::Application::ImGuiState>();
      imguiPool = state.imguiPool;

      // ECS-owned textures unregister their ImGui descriptors during teardown,
      // so destroy them before shutting down ImGui or its pool.
      world.reset();
      ImGui_ImplVulkan_Shutdown();
      ImGui_ImplSDL3_Shutdown();
      ImNodes::DestroyContext();
      ImGui::DestroyContext();
      vkDestroyDescriptorPool(vulkan.device, imguiPool, nullptr);
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
    // ren::addSSAO(G, gbufferDepth, gbufferNormal, ssao);



    // G.pass("gizmo").execute([&](ren::GraphRunContext &ctx) {});


    // GraphHandle buzz;
    // G.pass("Fizbuzz").createColorAttachment("buzz", {.absoluteSize = glm::uvec2(512, 512)}, buzz).render([&](ren::GraphRenderPassContext &ctx) {});
    // for (int i = 0; i < 128; i++) {
    //   G.pass("gadget").execute([&](ren::GraphRunContext &ctx) { printf("Gadget pass %d\n", i); });
    // }


    // auto program = ren::make<ren::ShaderProgram>("test/drawparams_temp_test");
    // auto material = program->getReflection()->getMaterialSchema();
    // if (material.isNone()) {
    //   abort();
    // }


    ren::PipelineStateObject squareAPSO;
    squareAPSO.debugName = "Bindless Square A";
    squareAPSO.program = make<ShaderProgram>("demo/square_a");
    squareAPSO.cullMode = ren::CullMode::None;
    squareAPSO.depthTest = false;
    squareAPSO.depthWrite = false;

    ren::PipelineStateObject squareBPSO = squareAPSO;
    squareBPSO.debugName = "Bindless Square B";
    squareBPSO.program = make<ShaderProgram>("demo/square_b");

    auto makeSquare = [] {
      ren::MeshBuilder builder;
      auto face = builder.beginFace();
      face.vertex(glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(0.0f, 0.0f));
      face.vertex(glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(1.0f, 0.0f));
      face.vertex(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(1.0f, 1.0f));
      face.vertex(glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f), glm::vec2(0.0f, 1.0f));
      face.end();
      return builder.stampOut();
    };


    // Generate a unit-cube mesh for testing purposes.
    auto makeCube = [] {
      ren::MeshBuilder builder;

      auto addFace = [&builder](glm::vec3 bottomLeft, glm::vec3 bottomRight, glm::vec3 topRight, glm::vec3 topLeft, glm::vec3 normal) {
        auto firstTriangle = builder.beginFace();
        firstTriangle.vertex(bottomLeft, normal, glm::vec2(0.0f, 0.0f));
        firstTriangle.vertex(bottomRight, normal, glm::vec2(1.0f, 0.0f));
        firstTriangle.vertex(topRight, normal, glm::vec2(1.0f, 1.0f));
        firstTriangle.end();

        auto secondTriangle = builder.beginFace();
        secondTriangle.vertex(bottomLeft, normal, glm::vec2(0.0f, 0.0f));
        secondTriangle.vertex(topRight, normal, glm::vec2(1.0f, 1.0f));
        secondTriangle.vertex(topLeft, normal, glm::vec2(0.0f, 1.0f));
        secondTriangle.end();
      };

      constexpr float h = 0.5f;

      addFace({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0.0f, 0.0f, 1.0f});
      addFace({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0.0f, 0.0f, -1.0f});
      addFace({h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1.0f, 0.0f, 0.0f});
      addFace({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1.0f, 0.0f, 0.0f});
      addFace({-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0.0f, 1.0f, 0.0f});
      addFace({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0.0f, -1.0f, 0.0f});

      return builder.stampOut();
    };

    auto squareAMesh = makeSquare();
    auto squareBMesh = makeSquare();

    std::array<u8, 16> warmPixels{255, 80, 32, 255, 255, 190, 32, 255, 255, 190, 32, 255, 255, 80, 32, 255};
    std::array<u8, 16> coolPixels{32, 120, 255, 255, 32, 255, 190, 255, 32, 255, 190, 255, 32, 120, 255, 255};
    auto warmTexture = Texture::create("bindless-warm", 2, 2, warmPixels.data());
    auto coolTexture = Texture::create("bindless-cool", 2, 2, coolPixels.data());



    ren::PipelineStateObject cubePSO;
    cubePSO.debugName = "Cube";
    cubePSO.program = make<ShaderProgram>("demo/cube");
    cubePSO.cullMode = ren::CullMode::None;
    cubePSO.depthTest = true;
    cubePSO.depthWrite = true;
    cubePSO.fillMode = ren::FillMode::Solid;
    auto cubeMesh = makeCube();


    // A small shared palette rather than a unique color per cube, so that the
    // material array is genuinely deduplicated (100 draws, 8 materials).
    static constexpr std::array<glm::vec3, 8> kCubePalette{
        glm::vec3(0.90f, 0.30f, 0.24f), glm::vec3(0.95f, 0.61f, 0.07f), glm::vec3(0.95f, 0.85f, 0.24f), glm::vec3(0.18f, 0.80f, 0.44f),
        glm::vec3(0.10f, 0.74f, 0.61f), glm::vec3(0.20f, 0.60f, 0.86f), glm::vec3(0.61f, 0.35f, 0.71f), glm::vec3(0.93f, 0.94f, 0.95f),
    };

    struct Cube {
      glm::vec3 position;
      glm::vec3 rotation;  // Radians
      glm::vec3 scale = glm::vec3(1.0f);
      u32 paletteIndex = 0;
      glm::vec3 velocity = glm::vec3(0.0f);

      glm::mat4 modelMatrix() const {
        glm::mat4 m(1.0f);
        m = glm::translate(m, position);
        m = glm::rotate(m, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
        m = glm::scale(m, scale);
        return m;
      }
    };

    const u32 numCubes = 100;
    std::vector<Cube> cubes;
    for (u32 i = 0; i < numCubes; i++) {
      Cube c;
      const float x = static_cast<float>(i % 10) - 4.5f;
      const float y = static_cast<float>((i / 10) % 5) - 2.0f;
      const float z = -5.0f - static_cast<float>(i / 50.0f) * 5.0f;
      c.position = glm::vec3(x * 2.0f, y * 2.0f, z);
      c.rotation = glm::vec3(ren::randomFloat(0.0f, glm::two_pi<float>()), ren::randomFloat(0.0f, glm::two_pi<float>()),
                             ren::randomFloat(0.0f, glm::two_pi<float>()));
      c.paletteIndex = i % static_cast<u32>(kCubePalette.size());
      // random velocity
      c.velocity = ren::randomDirection();
      cubes.push_back(c);
    }


    float renderScaleTemp = 1.0f;

    // TODO: Move me to a logical class, not here as static variables.
    //       Maybe these should live in a WSI abstraction class?
    static bool swapchainNeedsRebuild = false;
    static bool isResizing = false;



    G.pass("gizmo").execute([&](ren::GraphRunContext &ctx) {});

    SDL_RaiseWindow(this->window);

    while (this->running) {
      const auto frameStart = std::chrono::steady_clock::now();
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



          {
            REN_PROFILE_SCOPE("ImGui_ImplSDL3_ProcessEvent");
            if (ImGui_ImplSDL3_ProcessEvent(&e)) {
              continue;
            }
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

      auto &cam = ren::Camera::get();
      cam.update(deltaTime);

      renderer->beginFrame();
      auto &frame = ren::getFrameUnit();
      const glm::vec2 frameSize{frame.deviceImage->getWidth(), frame.deviceImage->getHeight()};
      auto viewMatrix = cam.view_matrix();
      auto projMatrix = ren::Camera::projectionMatrix(frameSize.x, frameSize.y);
      frame.setFrameGlobals({
          .time = time,
          .deltaTime = deltaTime,
          .frameNumber = static_cast<u32>(vulkan.frame_number),
          .renderSize = frameSize,
          .inverseRenderSize = 1.0f / frameSize,
          .viewMatrix = viewMatrix,
          .projMatrix = projMatrix,
      });

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



      if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("View")) {
          if (ImGui::MenuItem("ImGui Demo")) {
            ren::logUI("ImGui Demo", [](auto &ctx) { ImGui::ShowDemoWindow(&ctx.opened); });
          }
          ImGui::EndMenu();
        }


        // Simply print the framerate in the menu bar.
        char buf[64];
        snprintf(buf, sizeof(buf), "%4d FPS", (int)framerateCounter.getAverageFramerate());
        ImGui::MenuItem(buf);

        ImGui::EndMainMenuBar();
      }

      /**
       * Show the console log inspection.
       * see: https://nickw.io/post/logging-user-interfaces
       */
      ren::inspectLog();

      /**
       * Tick the world and run lua update functions. This is where all the
       * game logic happens, in effect. This currently runs on the main thread,
       * but needs to be moved to a worker thread in the future to avoid
       * blocking the main render thread with game logic.
       */
      {
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


      /**
       * Calculate the size of the render target based on the current window size.
       */
      float width = (float)windowWidth;
      float height = (float)windowHeight;
      float targetHeight = height * renderScaleTemp;
      float scale = targetHeight / height;
      scale *= renderScaleTemp;
      width *= scale;
      height *= scale;
      auto renderSize = glm::uvec2(width, height);
      G.startFrame(renderSize);
      try {
        G.run(*renderer);
        // G.inspect();
      } catch (const std::exception &e) {
        ren::println("✗ RenderPassTask execution failed: {}", e.what());
      }


      // ImGui::Begin("Material");
      // material.unwrap().inspect();
      // ImGui::End();



      auto enc = frame.getMainCommandEncoder();
      auto penc = enc->beginRenderPass(*renderer->getDisplayPass(), *frame.renderTarget);
      {
        REN_PROFILE_SCOPE("FullScreenPass");
        if (0) {
          REN_PROFILE_SCOPE("RenderBackgroundTriangle");
          penc.bindImmediateMesh(squareAMesh->vertices, squareAMesh->indices);
          auto squareA = penc.bindGraphics(squareAPSO);
          auto squareAConstants = squareA.root("root");
          squareAConstants.set("offset", glm::vec2(-0.5f, 0.0f)).set("scale", 0.32f).set("pulseAmount", 0.08f).set("image", warmTexture);

          squareA.drawIndexed({.vertexCount = static_cast<u32>(squareAMesh->indices.size())});

          penc.bindImmediateMesh(squareBMesh->vertices, squareBMesh->indices);
          auto squareB = penc.bindGraphics(squareBPSO);
          auto squareBConstants = squareB.root("root");
          auto squareBTransform = squareBConstants.get("transform");
          squareBTransform.set("center", glm::vec2(0.5f, 0.0f)).set("extent", 0.32f).set("rotationSpeed", 0.35f);
          squareBConstants.set("tint", glm::vec4(1.0f)).set("pattern", coolTexture);
          squareB.drawIndexed({.vertexCount = static_cast<u32>(squareBMesh->indices.size())});
        }


        if (1) {
          REN_PROFILE_SCOPE("Cubes");


          auto start = std::chrono::high_resolution_clock::now();

          // Run an n-body simulation on the cubes, applying gravity between them.
          for (auto &c1 : cubes) {
            for (auto &c2 : cubes) {
              if (&c1 != &c2) {
                glm::vec3 toOther = c2.position - c1.position;
                float distance = glm::length(toOther);
                if (distance > 0.0f) {
                  // if they are closer than 1, bounce off.
                  if (distance < 1.0f) {
                    glm::vec3 normal = glm::normalize(toOther);
                    c1.velocity -= normal * 2.0f * glm::dot(c1.velocity, normal);
                  } else {
                    glm::vec3 gravityDir = glm::normalize(toOther);
                    float gravityStrength = 9.0f / (distance * distance);
                    c1.velocity += gravityDir * gravityStrength * deltaTime;
                  }
                }
              }
            }
          }



          penc.bindImmediateMesh(cubeMesh->vertices, cubeMesh->indices);
          auto cube = penc.bindGraphics(cubePSO);
          auto root = cube.root("root");

          struct alignas(16) ObjectData {
            glm::mat4 modelMatrix;
            glm::mat4 normalMatrix;
            u32 objectId;
            u32 _padding[3];
          };

          struct alignas(16) MaterialData {
            glm::vec4 color;
          };

          struct InstanceRecord {
            u32 objectIndex;
            u32 materialIndex;
          };
          static_assert(sizeof(ObjectData) == 144);
          static_assert(offsetof(ObjectData, normalMatrix) == 64);
          static_assert(offsetof(ObjectData, objectId) == 128);
          static_assert(sizeof(MaterialData) == 16);
          static_assert(sizeof(InstanceRecord) == 8);

          // The three arrays the batched root points at. Note the differing
          // cardinalities: materials are keyed by *material identity*, so the
          // palette is uploaded once no matter how many cubes reference it.
          auto objectData = penc.getArena().push<StorageBuffer<ObjectData>>(cubes.size());
          auto materialData = penc.getArena().push<StorageBuffer<MaterialData>>(kCubePalette.size());
          auto instanceData = penc.getArena().push<StorageBuffer<InstanceRecord>>(cubes.size());
          auto drawCommands = penc.getArena().push<IndirectBuffer<VkDrawIndexedIndirectCommand>>(cubes.size());

          {
            REN_PROFILE_SCOPE("Cubes - Update");
            for (size_t i = 0; i < kCubePalette.size(); i++) {
              materialData->hostData()[i] = MaterialData{.color = glm::vec4(kCubePalette[i], 1.0f)};
            }

            const u32 cubeIndexCount = static_cast<u32>(cubeMesh->indices.size());
            for (size_t i = 0; i < cubes.size(); i++) {
              auto &c = cubes[i];

              c.position += c.velocity * deltaTime;

              ObjectData data{};
              const glm::mat4 modelMatrix = c.modelMatrix();
              data.modelMatrix = modelMatrix;
              data.normalMatrix = glm::transpose(glm::inverse(modelMatrix));
              data.objectId = static_cast<u32>(i);
              objectData->hostData()[i] = data;

              instanceData->hostData()[i] = InstanceRecord{
                  .objectIndex = static_cast<u32>(i),
                  .materialIndex = c.paletteIndex,
              };

              // One command per draw. Every cube shares the immediate mesh for
              // now, so the index/vertex offsets are identical; these are what
              // vary once draws come out of the MegaMeshBuffer.
              drawCommands->hostData()[i] = VkDrawIndexedIndirectCommand{
                  .indexCount = cubeIndexCount,
                  .instanceCount = 1,
                  .firstIndex = 0,
                  .vertexOffset = 0,
                  .firstInstance = 0,
              };
            }
          }

          {
            REN_PROFILE_SCOPE("Cubes - DRAW");
            // One push constant, one draw, for the whole batch.
            root.set("instances", instanceData->devicePointer<InstanceRecord>());
            root.set("objects", objectData->devicePointer<ObjectData>());
            root.set("materials", materialData->devicePointer<MaterialData>());
            cube.drawIndexedIndirect(*drawCommands, static_cast<u32>(cubes.size()));
          }


          auto duration = std::chrono::high_resolution_clock::now() - start;
          auto durationMs = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();

          ImGui::Begin("Cubes");
          ImGui::Text("Cube count: %zu", cubes.size());
          ImGui::Text("Material buffer size: %zu bytes", materialData->getByteCount());
          ImGui::Text("Object buffer size: %zu bytes", objectData->getByteCount());
          ImGui::Text("Instance buffer size: %zu bytes", instanceData->getByteCount());
          ImGui::Text("Draw commands buffer size: %zu bytes", drawCommands->getByteCount());
          ImGui::Text("Setup time: %lld us", durationMs);

          ImGui::End();
        }

        /**
         * Finally, render the ImGui draw data that has been accumulated over
         * the course of the frame. This needs to be rendered at the end of the
         * frame to ensure that it appears on top of all other rendered content.
         */
        {
          REN_PROFILE_SCOPE("RenderImGui");

          ren::renderDebugLines(viewMatrix, projMatrix);
          ImGui::Render();
          ImGui::UpdatePlatformWindows();
          ImGui::RenderPlatformWindowsDefault();
          auto commandBuffer = penc.buf();  // Gross Leakage...
          ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }
      }

      penc.end();

      renderer->endFrame();

      if (kMaxFPS.get() > 0) {
        const auto frameDuration = std::chrono::duration<double>(1.0 / kMaxFPS.get());
        std::this_thread::sleep_until(frameStart + frameDuration);
      }
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
