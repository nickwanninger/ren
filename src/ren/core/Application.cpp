
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

static const char *introGraphic =
    "Welcome to\n"
    "   \x1b[31m_ __\x1b[0m \x1b[32m___\x1b[0m \x1b[34m_ __\x1b[0m  \n"
    "  \x1b[31m| '__/\x1b[0m \x1b[32m_ \\\x1b[0m \x1b[34m'_ \\\x1b[0m \n"
    "  \x1b[31m| |\x1b[0m \x1b[32m|  __/\x1b[0m \x1b[34m| | |\x1b[0m\n"
    "  \x1b[31m|_|\x1b[0m  \x1b[32m\\___|\x1b[0m\x1b[34m_| |_|\x1b[0m\n";

static ren::Application *g_application = nullptr;
namespace ren {

  Application &Application::get(void) { return *g_application; }
  Application::Application(const std::string &app_name, glm::uvec2 window_size, Box<Window> window)
      : window(window ? std::move(window) : makeBox<Window>()) {

    printf("%s\n", introGraphic);

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
      this->window->initialize(vulkan, desc);
    }

    vulkan->selectDevice(this->window->getSurface());

    // TODO: make this a resource instead of a value on Application.
    this->renderer = make<Renderer>(vulkan);

    // The swapchain allocates frame resources from the renderer, so it comes last.
    this->window->rebuildSwapchain();

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
    am.addDefaultSources();

    // Configure the lua interpreter to pull from the asset manager.
    am.configureLua(lua);

    {
      // Backup the old print, since we override it with our own.
      lua_getglobal(L, "print");
      lua_setglobal(L, "_old_print");


      sol::state_view lua(L);
      // Define our replacement print function
      auto inspector_print = [](sol::variadic_args va, sol::this_state ts) {
        std::ostringstream oss;
        sol::state_view lua(ts);
        sol::function tostring = lua["tostring"];

        bool first = true;
        for (auto v : va) {
          if (!first) {
            oss << " ";  // Separate with spaces (unlike lua, which is tabs)
          }
          first = false;

          // Convert to string using Lua's tostring
          sol::object s = tostring(v);
          oss << s.as<std::string>();
        }

        std::string line = oss.str();
        std::size_t beg = 0u;
        for (std::size_t i = 0u; i < line.size(); ++i) {
          if (line[i] == '\n') {
            ren::println("{}", line.substr(beg, i - beg));
            beg = i + 1u;
          }
        }
        if (beg < line.size()) {
          ren::println("{}", line.substr(beg, line.size() - beg));
        }
      };
      lua.set_function("print", inspector_print);
    }

    // Ensure we have a megamesh buffer. (TODO: move this somewhere non-global.)
    ren::ensureResource<ren::MegaMeshBuffer>();

    this->window->initializeUI();


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

    // Last, so that the window's content sees a fully set up application.
    this->window->onInitialize();
  }

  Application::~Application() {
    REN_PROFILE_FUNCTION();
    this->renderer->waitForIdle();
    // Not quite sure when to tear down lua yet.

    // Call exit callbacks
    for (auto &func : exitCallbacks) {
      func();
    }

    // ECS-owned textures unregister their ImGui descriptors during teardown,
    // so destroy them before the window shuts down ImGui.
    world.reset();

    // Close the window (ImGui, swapchain, surface, SDL window), then nuke the renderer.
    this->window.reset();
    this->renderer.reset();
  }




  // This function is the main loop of the application. It will run until the
  // application is closed, and will handle all events, updating the ECS world,
  // and rendering the scene (along with other fullscreen passes like ImGui.)
  void Application::run() {
    auto startTime = std::chrono::high_resolution_clock::now();
    auto lastTime = startTime;
    // The deadline of the next frame. It moves forward by a fixed period, so
    // oversleep in one frame does not make the next frame later.
    auto nextFrame = std::chrono::steady_clock::now();
    SDL_Event e;

    SDL_RaiseWindow(window->getHandle());

    while (this->running) {
      // As soon as possible, grab the current time and compute the delta time since the last frame.
      const auto frameStart = std::chrono::high_resolution_clock::now();
      float time = std::chrono::duration<float, std::chrono::seconds::period>(frameStart - startTime).count();
      auto deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(frameStart - lastTime).count();
      this->timeSeconds = time;
      lastTime = frameStart;

      {
        REN_PROFILE_SCOPE("Frame");
        {
          REN_PROFILE_SCOPE("SDL Poll");
          // Handle events on queue
          while (SDL_PollEvent(&e) != 0) {
            REN_PROFILE_SCOPE("SDL Dispatch");

            window->handleEvent(e);

            switch (e.type) {
              case SDL_EVENT_QUIT: {
                this->running = false;
                break;
              }
              default: {
                break;
              }
            }
          }
        }


        // Handle swapchain rebuild if needed, and the state machine allows.
        if (window->needsRebuild()) {
          window->rebuildSwapchain();
          continue;
        }

        if (!running) {
          // TODO: cleanup!
          break;
        }


        {
          REN_PROFILE_SCOPE("Begin Frame");
          renderer->beginFrame(*window);
          window->beginFrame();
        }


        {
          REN_PROFILE_SCOPE("Tick");
          REN_PROFILE_COUNTER("delta_time", deltaTime * 1000.0f);

          window->onUpdate(deltaTime);


          if (1) {
            REN_PROFILE_SCOPE("Lua");
            // Update lua globals
            lua.globals()["delta_time"] = deltaTime;
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



        {
          REN_PROFILE_SCOPE("render");
          window->render(ren::getFrameUnit());
        }

        {
          REN_PROFILE_SCOPE("End Frame");
          renderer->endFrame(*window);
        }
      }


      {
        // FPS limiting at the end of frame (after submission) to reduce input latency.
        // If the window is not focused, limit to 30 FPS to reduce CPU/GPU usage.
        bool isFocused = window->hasFocus();
        int maxFps = isFocused ? kMaxFPS.get() : std::min(kIdleFPS.get(), kMaxFPS.get());

        if (maxFps > 0) {
          const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / maxFps));
          nextFrame += period;
          // If we are more than one period late (a hitch or a breakpoint), do
          // not try to catch up with a burst of short frames.
          const auto now = std::chrono::steady_clock::now();
          if (nextFrame < now - period) {
            nextFrame = now;
          }
          std::this_thread::sleep_until(nextFrame);
        } else {
          nextFrame = std::chrono::steady_clock::now();
        }
      }
    }
    renderer->waitForIdle();
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
