
#include <ren/core/Application.h>
#include <ren/core/AutoPlugin.h>
#include <ren/layers/ImGuiLayer.h>

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


#include <ren/layers/inspector/AssimpSceneInspector.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <ren/core/Systems.h>
#include <ren/core/SceneRenderer.h>


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

    // world.set_threads(6);

    this->globalEventEntity = world.entity("ren::events");

    // Enable the flecs world rest api
    ren::world().set<flecs::Rest>({});
    ren::world().import <flecs::stats>();

    ren::initPhases(ren::world());

    // Create an asset manager. This part of the heirarchy will hold
    // loaded assets.
    world.emplace<AssetManager>();

    auto scene = ren::world().entity("scene");

    SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    this->window =
        SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         window_size.x, window_size.y, window_flags);
    if (!this->window) {
      fmt::println("SDL_CreateWindow Error: {}", SDL_GetError());
      throw std::runtime_error("Failed to create SDL window");
    }

    this->renderer = makeRef<Renderer>(this->window);


    this->sceneLayer = makeRef<SceneLayer>(*this);
    this->layerStack.pushLayer(sceneLayer);

    // Add the ImGuiLayer to the stack.
    this->imguiLayer = makeRef<ImGuiLayer>(*this);
    this->layerStack.pushLayer(imguiLayer);


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


    ren::AutoPlugin::registerPlugins(*this);
  }

  Application::~Application() {
    REN_PROFILE_FUNCTION();
    this->renderer->waitForIdle();

    // Clear the layer stack
    this->layerStack.clear();

    this->sceneLayer.reset();
    this->imguiLayer.reset();

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




      {
        REN_PROFILE_SCOPE("ImGui New Frame");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());

        int windowWidth, windowHeight;
        SDL_GetWindowSize(getWindow(), &windowWidth, &windowHeight);
        ImGuizmo::SetRect(0.0f, 0.0f, windowWidth, windowHeight);


        // Before rendering, lets create a dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                     ImGuiDockNodeFlags_PassthruCentralNode);
      }

      {
        REN_PROFILE_SCOPE("Tick");
        world.progress(deltaTime);
      }





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



        // world.run_pipeline<ren::RenderDebug>(deltaTime);
        // ImGui::Begin("ECS World");
        // struct EntityTreeInspector {
        //   static inline void drawEntity(flecs::entity entity) {
        //     ImGui::PushID((u64)entity.id());
        //     const char *nameBuffer;
        //     if (auto name = entity.name(); name.length() != 0) {
        //       nameBuffer = name.c_str();
        //     } else if (auto nameComp = entity.try_get<comp::Name>()) {
        //       nameBuffer = nameComp->name.c_str();
        //     } else {
        //       nameBuffer = "Unnamed Entity";
        //     }
        //     if (ImGui::TreeNode(nameBuffer)) {
        //       entity.children([&](flecs::entity child) { drawEntity(child); });
        //       ImGui::TreePop();
        //     }
        //     ImGui::PopID();
        //   };
        // };
        // EntityTreeInspector::drawEntity(ren::world().lookup("scene"));
        // ImGui::End();


        sceneRenderer.inspect();
        layerStack.onImGuiRender(deltaTime);

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


}  // namespace ren
