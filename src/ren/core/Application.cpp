
#include <ren/core/Application.h>

#include <algorithm>
#include <cmath>
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
#include <ren/renderer/Viewport.h>
#include <ren/renderer/graph/RenderPassTask.h>
#include <ren/renderer/graph/tasks/DepthPrepassTask.h>
#include <ren/renderer/graph/tasks/SSAOTask.h>
#include <ren/renderer/graph/tasks/ShadowMapTask.h>
#include <ren/renderer/graph/tasks/GBufferTask.h>
#include <ren/renderer/Sampler.h>
#include <ren/renderer/shader/ShaderCache.h>

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
static ren::Flag<int> kIdleFPS("idle-fps", 15, "Maximum framerate when the application is idle, 0 = vsync or uncapped.");
static ren::Flag<bool> kFullscreen("fullscreen", false, "Start the application in fullscreen mode.");
static ren::Flag<bool> kHighDPI("high-dpi", true, "Enable high DPI support.");
static ren::Flag<bool> kImGuiViewports("imgui-viewports", false, "Enable ImGui viewports. (experimental)");

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


    // The Vulkan instance comes first, because a window needs it to make its
    // surface. The main window's surface then selects the device.
    auto vulkan = make<VulkanInstance>();

    {
      WindowDesc desc;
      desc.title = fmt::format("{} - Ren {}", app_name, REN_VERSION);
      desc.size = window_size;
      desc.flags = SDL_WINDOW_RESIZABLE;
      if (kHighDPI.get()) {
        desc.flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
      }
      if (kFullscreen.get()) {
        desc.flags |= SDL_WINDOW_FULLSCREEN;
      }
      this->windows.push_back(makeBox<Window>(vulkan, desc));
    }

    vulkan->selectDevice(getMainWindow().getSurface());

    // TODO: make this a resource instead of a value on Application.
    this->renderer = make<Renderer>(vulkan);

    // The swapchain allocates frame resources from the renderer, so it comes last.
    getMainWindow().rebuildSwapchain();

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

    // Close the windows (swapchain, surface, SDL window), then nuke the renderer.
    this->windows.clear();
    this->renderer.reset();
  }


  Window *Application::findWindow(SDL_Window *handle) const {
    for (const auto &window : this->windows) {
      if (window->getHandle() == handle) {
        return window.get();
      }
    }
    return nullptr;
  }




  // This function is the main loop of the application. It will run until the
  // application is closed, and will handle all events, updating the ECS world,
  // and rendering the scene (along with other fullscreen passes like ImGui.)
  void Application::run() {
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastTime = startTime;
    SDL_Event e;


    auto &vulkan = ren::getVulkan();
    FramerateCounter framerateCounter;



    // ren::RenderGraph G;

    ren::EditorViewport viewportA;
    viewportA.position = {0.0f, 1.5f, 4.5f};
    viewportA.angles = {-0.08f, 0.0f, 0.0f};
    viewportA.showInputOverlay = true;


    ren::Material exampleMaterial("demo/cube");

    // ren::GraphHandle nullHandleOut;
    // ren::GraphHandle ssao;
    // ren::GraphHandle gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth;
    // auto &gbp = ren::addGBuffer(G, gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth);
    // ren::addSSAO(G, gbufferDepth, gbufferNormal, ssao);


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

    std::array<u8, 16> warmPixels{255, 80, 32, 255, 255, 190, 32, 255, 255, 190, 32, 255, 255, 80, 32, 255};
    std::array<u8, 16> coolPixels{32, 120, 255, 255, 32, 255, 190, 255, 32, 255, 190, 255, 32, 120, 255, 255};
    auto warmTexture = Texture::create("bindless-warm", 2, 2, warmPixels.data());
    auto coolTexture = Texture::create("bindless-cool", 2, 2, coolPixels.data());
    auto stoneTexture = ren::getAsset<ren::Texture>("assets/test/stone.png");




    ren::PipelineStateObject cubePSO;
    cubePSO.debugName = "Cube";
    cubePSO.program = ren::getShader("demo/cube");
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
      // A quaternion rather than Euler angles: collisions feed in an angular
      // velocity, and there is no sane way to integrate one into a fixed-order
      // XYZ triple without fighting gimbal lock.
      glm::quat rotation = {1.0f, 0.0f, 0.0f, 0.0f};
      glm::vec3 scale = glm::vec3(1.0f);
      u32 paletteIndex = 0;
      glm::vec3 velocity = glm::vec3(0.0f);
      glm::vec3 angularVelocity = glm::vec3(0.0f);  // Radians/sec, world space.

      glm::mat4 modelMatrix() const {
        return glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(glm::normalize(rotation)) * glm::scale(glm::mat4(1.0f), scale);
      }
    };

    const u32 numCubes = 1000;
    i32 range = 20;
    std::vector<Cube> cubes;
    for (u32 i = 0; i < numCubes; i++) {
      Cube c;
      c.position = glm::vec3(ren::randomInt(-range, range), ren::randomInt(-range, range), ren::randomInt(-range, range));
      c.paletteIndex = i % static_cast<u32>(kCubePalette.size());
      c.velocity = ren::randomDirection();
      cubes.push_back(c);
    }


    float renderScaleTemp = 1.0f;




    // G.pass("gizmo").execute([&](ren::GraphRunContext &ctx) {});

    SDL_RaiseWindow(getWindow());

    while (this->running) {
      REN_PROFILE_SCOPE("Frame");

      // As soon as possible, grab the current time and compute the delta time since the last frame.
      const auto frameStart = std::chrono::high_resolution_clock::now();
      float time = std::chrono::duration<float, std::chrono::seconds::period>(frameStart - startTime).count();
      auto deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(frameStart - lastTime).count();
      this->timeSeconds = time;
      lastTime = frameStart;

      {
        REN_PROFILE_SCOPE("SDL Poll");
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
          REN_PROFILE_SCOPE("SDL Dispatch");

          if (SDL_Window *target = SDL_GetWindowFromEvent(&e)) {
            if (Window *window = findWindow(target)) {
              window->handleEvent(e);
            }
          }

          switch (e.type) {
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
      }


      // Handle swapchain rebuild if needed, and the state machine allows.
      auto &mainWindow = getMainWindow();
      if (mainWindow.needsRebuild()) {
        REN_PROFILE_SCOPE("Rebuild Swapchain");
        mainWindow.rebuildSwapchain();
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

      if (!running) {
        // TODO: cleanup!
        break;
      }

      // TOOD: Move this to some kind of camera system.  The problem is that we
      // need the frame globals to be set before we can render, and the camera
      // is part of the frame globals.
      auto &cam = ren::Camera::get();
      cam.update(deltaTime);

      renderer->beginFrame(mainWindow);
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
        snprintf(buf, sizeof(buf), "%4d FPS DT=%.3f", (int)framerateCounter.getAverageFramerate(), deltaTime);
        ImGui::MenuItem(buf);

        ImGui::EndMainMenuBar();
      }

      /**
       * Show the console log inspection.
       * see: https://nickw.io/post/logging-user-interfaces
       */
      ren::inspectLog();


      ImGui::Begin("ShaderCache");
      renderer->getShaderCache().inspect();
      ImGui::End();


      ImGui::Begin("Material Inspector");
      exampleMaterial.inspect();
      ImGui::End();
      /**
       * Tick the world and run lua update functions. This is where all the
       * game logic happens, in effect. This currently runs on the main thread,
       * but needs to be moved to a worker thread in the future to avoid
       * blocking the main render thread with game logic.
       */
      {
        REN_PROFILE_SCOPE("Tick");
        world.progress(deltaTime);
        // Tick the ECS
        if (0) {
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
      }



      viewportA.drawWindow("Viewport A", deltaTime);
      viewportA.render(*renderer);

      auto enc = frame.getMainCommandEncoder();
      auto penc = enc->beginRenderPass(*renderer->getDisplayPass(), *frame.renderTarget);
      {
        REN_PROFILE_SCOPE("FullScreenPass");

        if (0) {
          REN_PROFILE_SCOPE("Cubes");

          // Run an n-body simulation on the cubes: pairwise gravity plus an
          // impulse-based collision response. Each unordered pair is visited
          // exactly once so both the force and the impulse stay symmetric and
          // momentum is conserved.
          {
            REN_PROFILE_SCOPE("Cubes - Simulate");

            constexpr float kRadius = 0.5f;                     // Half-extent of a unit cube.
            constexpr float kContactDistance = 2.0f * kRadius;  // Touching when centers are this close.
            constexpr float kRestitution = 0.96f;               // 1.0 would be perfectly elastic.
            constexpr float kGravity = 9.0f;
            constexpr float kSoftening = 0.25f;  // Keeps 1/r^2 finite as bodies approach.
            constexpr float kFriction = 0.35f;   // Tangential impulse as a fraction of the normal one.
            constexpr float kMaxSpin = 12.0f;    // Radians/sec, purely to keep the sim watchable.

            // Unit mass, and the analytic inertia of a uniform cube of side s
            // about its center: I = m*s^2/6, which is isotropic, so the tensor
            // never has to be rotated into world space.
            constexpr float kInvMass = 1.0f;
            constexpr float kInvInertia = 6.0f / (kContactDistance * kContactDistance);

            // A single step across a long frame lets bodies tunnel through each
            // other and makes the 1/r^2 term explode, so the frame is split into
            // micro steps of at most kMaxStep. The step count is capped so a
            // hitch (or a breakpoint) cannot drag the sim into a death spiral -
            // past that point the simulation just runs slower than wall clock.
            constexpr float kMaxStep = 1.0f / 240.0f;
            constexpr int kMaxSubsteps = 16;

            const int substeps = glm::clamp(static_cast<int>(std::ceil(deltaTime / kMaxStep)), 1, kMaxSubsteps);
            const float dt = std::min(deltaTime, kMaxStep * static_cast<float>(kMaxSubsteps)) / static_cast<float>(substeps);
            if (0) {
              for (int step = 0; step < substeps; step++) {
                for (size_t i = 0; i < cubes.size(); i++) {
                  for (size_t j = i + 1; j < cubes.size(); j++) {
                    auto &a = cubes[i];
                    auto &b = cubes[j];

                    const glm::vec3 delta = b.position - a.position;
                    const float distanceSq = glm::dot(delta, delta);
                    if (distanceSq < 1e-6f) {
                      continue;
                    }

                    const float distance = std::sqrt(distanceSq);
                    const glm::vec3 normal = delta / distance;

                    // Gravity applies at every separation, contact included. Cutting
                    // it out while overlapping made pairs pop apart rather than settle.
                    const float accel = kGravity / (distanceSq + kSoftening);
                    a.velocity += normal * accel * dt;
                    b.velocity -= normal * accel * dt;

                    if (distance >= kContactDistance) {
                      continue;
                    }

                    // The contact point is faked: bodies are treated as spheres, so
                    // it is simply the surface point along the normal. A real
                    // cube-cube manifold would need SAT plus clipping. The lever arm
                    // is therefore parallel to the normal, which means the *normal*
                    // impulse produces no torque - all of the spin comes from the
                    // tangential (friction) impulse below.
                    const glm::vec3 contactA = normal * kRadius;
                    const glm::vec3 contactB = -normal * kRadius;

                    // Surface velocity, not center velocity: a spinning body drags
                    // its contact point along, which is what makes cubes that are
                    // already tumbling scrub against each other.
                    const glm::vec3 relative =
                        (b.velocity + glm::cross(b.angularVelocity, contactB)) - (a.velocity + glm::cross(a.angularVelocity, contactA));
                    const float approach = glm::dot(relative, normal);

                    // Only respond when the pair is actually closing. Reflecting
                    // unconditionally traps touching bodies, flipping their velocity
                    // every frame instead of letting them separate.
                    if (approach < 0.0f) {
                      // Equal masses, so the impulse splits evenly between the two.
                      const float normalImpulse = -(1.0f + kRestitution) * approach / (2.0f * kInvMass);
                      a.velocity -= normal * normalImpulse * kInvMass;
                      b.velocity += normal * normalImpulse * kInvMass;

                      // Coulomb friction along the sliding direction. Because the
                      // lever arms are parallel to the normal and the tangent is
                      // perpendicular to it, cross(cross(r, t), r) collapses to
                      // t*kRadius^2 and the denominator is a constant.
                      const glm::vec3 tangentVelocity = relative - normal * approach;
                      const float slidingSpeed = glm::length(tangentVelocity);
                      if (slidingSpeed > 1e-4f) {
                        const glm::vec3 tangent = tangentVelocity / slidingSpeed;
                        const float denom = 2.0f * kInvMass + 2.0f * kInvInertia * kRadius * kRadius;
                        // Clamped so friction can stop the sliding but never reverse it.
                        const float frictionImpulse = std::min(slidingSpeed / denom, kFriction * normalImpulse);
                        const glm::vec3 impulse = tangent * frictionImpulse;

                        a.velocity += impulse * kInvMass;
                        b.velocity -= impulse * kInvMass;
                        a.angularVelocity += kInvInertia * glm::cross(contactA, impulse);
                        b.angularVelocity += kInvInertia * glm::cross(contactB, -impulse);
                      }
                    }

                    // Separate whatever overlap the previous step left behind;
                    // otherwise the pair keeps re-colliding from inside each other.
                    const glm::vec3 correction = normal * ((kContactDistance - distance) * 0.5f);
                    a.position -= correction;
                    b.position += correction;
                  }
                }

                // Integrate inside the substep loop; advancing positions once per
                // frame would defeat the point of subdividing the step.
                for (auto &c : cubes) {
                  c.position += c.velocity * dt;

                  const float spin = glm::length(c.angularVelocity);
                  if (spin > kMaxSpin) {
                    c.angularVelocity *= kMaxSpin / spin;
                  }
                  if (spin > 1e-5f) {
                    // dq/dt = 0.5 * omega * q, renormalized to shed the drift that
                    // first-order integration introduces.
                    const glm::quat omega(0.0f, c.angularVelocity.x, c.angularVelocity.y, c.angularVelocity.z);
                    c.rotation = glm::normalize(c.rotation + 0.5f * dt * omega * c.rotation);
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

          struct alignas(4) MaterialData {
            glm::vec4 color;
            TextureHandle texture;
          };

          struct DrawRecord {
            u32 objectIndex;
            u32 materialIndex;
          };
          static_assert(sizeof(ObjectData) == 144);
          static_assert(offsetof(ObjectData, normalMatrix) == 64);
          static_assert(offsetof(ObjectData, objectId) == 128);
          static_assert(offsetof(MaterialData, texture) == 16);
          // static_assert(sizeof(MaterialData) == 32);
          static_assert(sizeof(DrawRecord) == 8);

          // The three arrays the batched root points at. Note the differing
          // cardinalities: materials are keyed by *material identity*, so the
          // palette is uploaded once no matter how many cubes reference it.
          auto objectData = penc.getArena().push<StorageBuffer<ObjectData>>(cubes.size());
          auto materialData = penc.getArena().push<StorageBuffer<MaterialData>>(kCubePalette.size());
          auto drawRecords = penc.getArena().push<StorageBuffer<DrawRecord>>(cubes.size());
          auto drawCommands = penc.getArena().push<IndirectBuffer<VkDrawIndexedIndirectCommand>>(cubes.size());

          {
            REN_PROFILE_SCOPE("Cubes - Update");
            for (size_t i = 0; i < kCubePalette.size(); i++) {
              materialData->hostData()[i] = MaterialData{.color = glm::vec4(kCubePalette[i], 1.0f), .texture = stoneTexture->handle()};
            }

            const u32 cubeIndexCount = static_cast<u32>(cubeMesh->indices.size());
            for (size_t i = 0; i < cubes.size(); i++) {
              auto &c = cubes[i];

              ObjectData data{};
              const glm::mat4 modelMatrix = c.modelMatrix();
              data.modelMatrix = modelMatrix;
              data.normalMatrix = glm::transpose(glm::inverse(modelMatrix));
              data.objectId = static_cast<u32>(i);
              objectData->hostData()[i] = data;

              drawRecords->hostData()[i] = DrawRecord{
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
            root.set("draws", drawRecords->devicePointer<DrawRecord>());
            root.set("objects", objectData->devicePointer<ObjectData>());
            root.set("materials", materialData->devicePointer<MaterialData>());
            cube.drawIndexedIndirect(*drawCommands, static_cast<u32>(cubes.size()));
          }
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
          auto commandBuffer = penc.buf();  // Gross Leakage... But it is needed.
          ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }
      }

      // End the full screen render pass, and submit the command buffer to the GPU for execution.
      penc.end();
      renderer->endFrame(mainWindow);


      // 3. --- VULKAN MULTI-VIEWPORT PROCESSING ---
      if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        // Computes positioning and updates platform windows
        ImGui::UpdatePlatformWindows();

        // Renders and presents secondary windows automatically via the Vulkan backend
        ImGui::RenderPlatformWindowsDefault();
      }

      {
        REN_PROFILE_SCOPE("Frame Pacing");

        // FPS limiting at the end of frame (after submission) to reduce input latency.
        // If the window is not focused, limit to 30 FPS to reduce CPU/GPU usage.
        bool isFocused = SDL_GetWindowFlags(getWindow()) & SDL_WINDOW_INPUT_FOCUS;
        isFocused = true;
        int maxFps = isFocused ? kMaxFPS.get() : kIdleFPS.get();

        if (maxFps > 0) {
          const auto frameDuration = std::chrono::duration<double>(1.0 / maxFps);
          std::this_thread::sleep_until(frameStart + frameDuration);
        }
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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (kImGuiViewports.get()) {
      io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
      // FORCE OS WINDOW DECORATIONS ON DETACHED VIEWPORTS
      io.ConfigViewportsNoDecoration = false;
    }

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
