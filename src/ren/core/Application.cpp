
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

static ren::Application *g_application = nullptr;
namespace ren {

  Application &Application::get(void) { return *g_application; }
  Application::Application(const std::string &app_name, glm::uvec2 window_size) {
    g_application = this;
    // Initialize the SDL window
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
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


    ren::PipelineCache pipelineCache;
    auto &vulkan = ren::getVulkan();

    ren::DescriptorLayoutCache descriptorLayoutCache;


    // ---------------------------------------------------

    // Okay. lets make a simple g buffer opaque pass.
    RenderPass::Description renderPassDesc;
    renderPassDesc.name = "GBuffer Pass";
    // Add a few attachments to the render pass for the gbuffer.

    // HDR emissive data.
    renderPassDesc.addColorAttachment("emissive", VK_FORMAT_R8G8B8A8_UNORM);
    // Albedo/diffuse color data.
    renderPassDesc.addColorAttachment("albedo", VK_FORMAT_R8G8B8A8_UNORM);
    // World space normal data.
    renderPassDesc.addColorAttachment("normal", VK_FORMAT_R16G16B16A16_SNORM);
    // PBR data (R= metallic, G=roughness, B=?).
    renderPassDesc.addColorAttachment("pbr", VK_FORMAT_R8G8B8A8_UNORM);
    renderPassDesc.addDepthAttachment("depthStencil");


    auto renderPass = renderer->getRenderPassCache().get(renderPassDesc);




    float pixelScale = 3.0f;
    float fov = 90.0f;


    // now make a render target for the gbuffer.
    float renderAspect = 0.0f;
    bool targetValid = false;
    ref<RenderTarget> gbufferTarget = nullptr;
    // renderPass->createRenderTarget(ren::target_render_width, ren::target_render_height);
    // fmt::println("Created render target {}", (u64)gbufferTarget->getHandle());
    // Make an empty descriptor set layout for the gbuffer because for now we don't have any
    // textures or samplers.

    // TODO: abstract this crap. (Maybe reflect it from the shaders.)

    DescriptorLayoutInfo gbufferLayoutInfo;
    auto gbufferLayout = descriptorLayoutCache.createLayout(gbufferLayoutInfo);
    {
      REN_PROFILE_SCOPE("Create GBuffer Descriptor Set Layout");

      VkDescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      layoutInfo.bindingCount = 0;
      layoutInfo.pBindings = nullptr;

      if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, nullptr, &gbufferLayout) !=
          VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
      }
    }


    // but, to make those buffers, we need to make a render pipeline!
    // auto gbufferPipeline =
    //     StandardPipeline(renderPass, makeRef<VertexShader>("shaders/opaque.vert.spv"),
    //                      makeRef<FragmentShader>("shaders/opaque.frag.spv"), gbufferLayout);
    ren::PipelineStateObject gbufferPso;
    gbufferPso.debugName = "GBuffer PSO";
    gbufferPso.program = makeRef<ShaderProgram>("shaders/opaque");
    gbufferPso.descriptorSetLayout = gbufferLayout;  // TODO: extract this from the shaders!

    // Now we have a deferred rendering pipeline.



    DescriptorLayoutInfo blitLayoutInfo;
    // For now, we will simply put all the gbuffer textures in a single descriptor set
    // Albedo, normal, pbr, and depth
    blitLayoutInfo.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                              VK_SHADER_STAGE_FRAGMENT_BIT);  // Albedo
    blitLayoutInfo.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                              VK_SHADER_STAGE_FRAGMENT_BIT);  // Normal
    blitLayoutInfo.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                              VK_SHADER_STAGE_FRAGMENT_BIT);  // PBR
    blitLayoutInfo.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                              VK_SHADER_STAGE_FRAGMENT_BIT);  // Emissive
    blitLayoutInfo.addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                              VK_SHADER_STAGE_FRAGMENT_BIT);  // Depth

    auto blitLayout = descriptorLayoutCache.createLayout(blitLayoutInfo);
    // Lets make a blit pipeline.
    // This will be used to blit the gbuffer to the screen.
    // We will use a simple fullscreen quad to do this.
    auto blitPipeline = DisplayPipeline(
        renderer->getDisplayPass(),  // use the main render pass for full screen rendering
        makeRef<VertexShader>("shaders/display.vert.spv"),
        makeRef<FragmentShader>("shaders/display.frag.spv"), blitLayout);



    float modelScale = 1.0f;
    glm::vec3 modelRotation(0.0f);
    glm::vec3 modelPosition(0.0f);


    // Let's make a sampler for the gbuffers
    ren::Sampler gbufferSampler(VK_FILTER_NEAREST);

    std::vector<VkDescriptorSet> imguiTextures;

    auto refreshImGuiTextures = [&]() {
      // Update the ImGui textures with the new gbuffer textures.
      imguiTextures.clear();
      for (auto &attachment : gbufferTarget->getAttachments()) {
        imguiTextures.push_back(ImGui_ImplVulkan_AddTexture(
            gbufferSampler.getHandle(), attachment.texture->getImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
      }
    };


    // exit(0);

    long trianglesRendered = 0;

    auto render_test = [&]() {
      REN_PROFILE_SCOPE("My Render Test");
      auto &vulkan = ren::getVulkan();
      auto &frame = ren::getFrameData();

      // ---------------------- //
      int w, h;
      SDL_GetWindowSize(this->window, &w, &h);
      // grab the aspect ratio
      float windowAspect = (float)w / (float)h;
      // ---------------------- //

      if (windowAspect != renderAspect || targetValid == false) {
        renderer->waitForIdle();
        fmt::println("Window aspect ratio changed from {} to {}", renderAspect, windowAspect);
        // resize the gbuffer target.
        gbufferTarget = renderPass->createRenderTarget(w / pixelScale, h / pixelScale);
        renderAspect = windowAspect;
        refreshImGuiTextures();
        renderer->waitForIdle();
        targetValid = true;
      }


      auto cmd = ren::getFrameData().commandBuffer;
      frame.perf.begin(cmd, "test pass");

      renderer->withPass(*renderPass, *gbufferTarget, [&]() {
        trianglesRendered = 0;
        REN_PROFILE_SCOPE("Render Test Pass");

        // We'll just iterate over the meshes in the scene and render them with their transforms.
        auto view = sceneLayer->scene.getAllWith<comp::Mesh, comp::Transform>();


        renderer->bind(gbufferPso.program);
        // gbufferPipeline.bind(cmd);


        this->sceneLayer->camera.projection =
            glm::perspective(glm::radians(fov), renderAspect, 0.01f, 1000.0f);
        this->sceneLayer->camera.projection[1][1] *= -1;  // Vulkan thing.

        ren::MeshPushConstants pc;
        pc.view = this->sceneLayer->camera.view_matrix();
        pc.proj = this->sceneLayer->camera.projection;

        view.each([&](entt::entity id, const comp::Mesh &mesh, const comp::Transform &transform) {
          REN_PROFILE_SCOPE("Render Mesh");
          // fmt::println("Rendering mesh {} with transform {}", id, transform.getTransform());
          trianglesRendered += mesh.mesh->getIndexCount() / 3;
          ren::bind(cmd, *mesh.mesh->getIndexBuffer());
          ren::bind(cmd, *mesh.mesh->getVertexBuffer());

          pc.model = transform.getTransform();
          renderer->setPushConstants(pc);

          // draw a single triangle on the screen.
          vkCmdDrawIndexed(cmd, mesh.mesh->getIndexCount(), 1, 0, 0, 0);
        });
      });
      frame.perf.end(cmd, "test pass");
    };


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

      if (!running) break;

      // Get a frame from the swapchain.

      renderer->beginFrame();
      auto &frame = ren::getFrameData();

      auto frameStats = frame.perf.nextFrame(frame.commandBuffer);



      REN_PROFILE_COUNTER("Memory Usage MB", ren::getCurrentProcessRSS() / (1024.0 * 1024.0));


      render_test();

      printf("%p\n", gbufferTarget.get());
      gbufferTarget->transitionToShaderReadonly(frame.commandBuffer);

    

      renderer->withPass(*renderer->getDisplayPass(), *frame.renderTarget, [&]() {
        auto cmd = ren::getFrameData().commandBuffer;
        // Blit the gbuffer to the screen temporarily.
        {
          REN_PROFILE_SCOPE("Blit GBuffer");

          ren::DescriptorBuilder builder(descriptorLayoutCache, frame.descriptorAllocator);
          std::vector<VkDescriptorImageInfo> imageInfos;
          auto &attachments = gbufferTarget->getAttachments();
          for (int i = 0; i < attachments.size(); i++) {
            auto &attachment = attachments[i];
            // printf("Binding attachment %d: %s\n", i, attachment.name.c_str());
            imageInfos.push_back({gbufferSampler.getHandle(), attachment.texture->getImageView(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
          }

          for (int i = 0; i < attachments.size(); i++) {
            builder.bindImage(i, &imageInfos[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_FRAGMENT_BIT);
          }

          VkDescriptorSet gbufferSet;
          builder.build(gbufferSet);
          // now bind that set.
          vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, blitPipeline.getLayout(), 0,
                                  1, &gbufferSet, 0, nullptr);

          frame.perf.begin(cmd, "Blit GBuffer");
          // bind the pipeline
          blitPipeline.bind(cmd);
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
          // float windowWidth = (float)ImGui::GetWindowWidth();
          // float windowHeight = (float)ImGui::GetWindowHeight();

          ImGuizmo::SetRect(0.0f, 0.0f, windowWidth, windowHeight);


          // Before rendering, lets create a dockspace
          ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                       ImGuiDockNodeFlags_PassthruCentralNode);
        }


        ImGui::Begin("G-Buffer Viewer");
        ImGui::Text("vulkan references: %zu", getVulkanRef().use_count());
        ImGui::DragFloat3("Position", &modelPosition.x, 0.01f);
        ImGui::DragFloat3("Rotation", &modelRotation.x, 0.01f);
        ImGui::DragFloat("Scale", &modelScale, 0.01f);
        ImGui::DragFloat("FOV", &fov, 0.1f, 30.0f, 360.0f);

        if (ImGui::DragFloat("Pixel Scale", &pixelScale, 0.1f, 1.0f, 64.0f)) {
          targetValid = false;
        }
        ImGui::Text("Triangles Rendered: %ld", trianglesRendered);

        ImGui::Separator();

        for (auto &[name, value] : frameStats) {
          ImGui::Text("%15s: %9.2fms", name.c_str(), value);
        }

        ImGui::Separator();


        auto attachments = gbufferTarget->getAttachments();
        for (int i = 0; i < attachments.size(); i++) {
          // if (i != 2) continue;
          auto &attachment = attachments[i];
          ImGui::Text("Attachment: %s", attachment.name.c_str());
          float width = ImGui::GetContentRegionAvail().x;
          float height = width / renderAspect;
          ImGui::Image((ImTextureID)imguiTextures[i], ImVec2(width, height));
          ImGui::Separator();
        }
        ImGui::End();


        ImGui::Begin("PSO");
        ImGui::Text("Pipeline cache size: %zu", renderer->getPipelineCache().size());
        json j = gbufferPso;
        ImGui::Text("Pipeline State Object: %s", j.dump(2).c_str());
        // hash of that object:
        ImGui::Text("Hash: %llu", ren::hash(gbufferPso));
        gbufferPso.renderInspector();
        ImGui::End();

        // graph.renderImGui();

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