
#include <ren/core/Application.h>
#include <ren/layers/ImGuiLayer.h>
#include <ren/renderer/pipelines/StandardPipeline.h>
#include <ren/renderer/pipelines/DisplayPipeline.h>
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

#include <ren/core/SceneRenderer.h>


static ren::Application *g_application = nullptr;
namespace ren {

  Application &Application::get(void) { return *g_application; }
  Application::Application(const std::string &app_name, glm::uvec2 window_size) {
    g_application = this;
    // Initialize the SDL window
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);

    // Enable the flecs world rest api
    ren::world().set<flecs::Rest>({});
    ren::world().import <flecs::stats>();


    // Create an asset manager. This part of the heirarchy will hold
    // loaded assets.
    world.emplace<AssetManager>();

    auto scene = ren::world().entity("scene");

    SDL_WindowFlags window_flags =
        (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    this->window =
        SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         window_size.x, window_size.y, window_flags);

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


    // Load a mesh scene using assimp, not tinygltf.
    Assimp::Importer importer;
    const char *path = "assets/test/meshes/simple_scene.glb";
    // const char *path = "/Users/nick/Desktop/sponza.glb";
    const aiScene *scene =
        importer.ReadFile(path, aiProcess_Triangulate | aiProcess_OptimizeMeshes | aiProcess_JoinIdenticalVertices);
    AssimpSceneInspector aiInspector(scene);



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


      auto currentTime = std::chrono::high_resolution_clock::now();
      float time =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime)
              .count();
      auto deltaTime =
          std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime)
              .count();
      lastTime = currentTime;



      {
        bool windowResized = false;
        REN_PROFILE_SCOPE("SDL Poll");
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
          REN_PROFILE_SCOPE("SDL Dispatch");
          eventsHandled++;
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




      world.progress(deltaTime);

      if (!running) break;

      // Get a frame from the swapchain.

      renderer->beginFrame();
      auto &frame = ren::getFrameData();

      auto frameStats = frame.perf.nextFrame(frame.commandBuffer);

      REN_PROFILE_COUNTER("Memory Usage MB", ren::getCurrentProcessRSS() / (1024.0 * 1024.0));




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
            textureBinder.bind(attachment.name, *attachment.texture);
          }
          textureBinder.apply();

          vkCmdDraw(cmd, 3, 1, 0, 0);
          frame.perf.end(cmd, "Blit GBuffer");
        }



        // Render IMGUI
        REN_PROFILE_SCOPE("ImGui Render");
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


        ImGui::Begin("ECS World");
        struct EntityTreeInspector {
          static inline void drawEntity(flecs::entity entity) {
            ImGui::PushID((u64)entity.id());
            const char *nameBuffer;
            if (auto name = entity.name(); name.length() != 0) {
              nameBuffer = name.c_str();
            } else if (auto nameComp = entity.try_get<comp::Name>()) {
              nameBuffer = nameComp->name.c_str();
            } else {
              nameBuffer = "Unnamed Entity";
            }
            if (ImGui::TreeNode(nameBuffer)) {
              entity.children([&](flecs::entity child) { drawEntity(child); });
              ImGui::TreePop();
            }
            ImGui::PopID();
          };
        };

        ren::world()
            .query_builder()
            .without(flecs::ChildOf, flecs::Wildcard)
            .build()
            .each(EntityTreeInspector::drawEntity);



        ImGui::Separator();

        auto start = std::chrono::steady_clock::now();
        InstrumentationTimer timer("Entity Query");
        ren::world().scope("scene").query<comp::Name>().each([&](Entity e, comp::Name &name) {
          ImGui::Text("Entity: %s %zu", name.name.c_str(), e.id());
        });
        auto end = std::chrono::steady_clock::now();

        auto highResStart = FloatingPointMicroseconds{start.time_since_epoch()};
        auto elapsedTime =
            std::chrono::time_point_cast<std::chrono::microseconds>(end).time_since_epoch() -
            std::chrono::time_point_cast<std::chrono::microseconds>(start).time_since_epoch();
        ImGui::Text("Query took %f ms",
                    std::chrono::duration<float, std::milli>(end - start).count());

        ImGui::End();


        ImGui::Begin("Gbuffer image pointers");
        ImGui::Text("GBuffer Target: %s", gbufferTarget ? "Valid" : "Invalid");

        auto displayRendertarget = [&](auto &rt, const char *name) {
          if (rt) {
            ImGui::Text("%s Target Size: %ux%u", name, rt->getWidth(), rt->getHeight());
            for (auto &attachment : rt->getAttachments()) {
              ImGui::Text("Attachment %s: %p, %p", attachment.name.c_str(),
                          attachment.texture->getImage(), attachment.texture->getImageView());
            }
            ImGui::Separator();
          }
        };

        displayRendertarget(gbufferTarget, "GBuffer");
        displayRendertarget(frame.renderTarget, "Display");

        ImGui::End();

        ImGui::Begin("assimp Inspector");
        aiInspector.render();
        ImGui::End();


        ImGui::Begin("Asset Manager");
        getAssetManager().inspect();
        ImGui::End();

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


      renderer->endFrame();



      // Update the layers.
      layerStack.onUpdate(deltaTime);
    }
    renderer->waitForIdle();
  }


}  // namespace ren
