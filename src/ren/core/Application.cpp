
#include <ren/core/Application.h>
#include <ren/layers/ImGuiLayer.h>
#include <ren/renderer/pipelines/StandardPipeline.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <imgui_impl_sdl2.h>
#include <ren/core/Entity.h>
#include <ren/assets/Mesh.h>

#include <ren/renderer/RenderGraph.h>

static ren::Application *g_application = nullptr;
namespace ren {

  Application &Application::get(void) { return *g_application; }
  Application::Application(const std::string &app_name, glm::uvec2 window_size) {
    g_application = this;
    // Initialize the SDL window
    SDL_Init(SDL_INIT_VIDEO);
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

    // scene.createEntity("Camera");
    // scene.createEntity("Cube");


    // auto s = scene.serialize();
    // std::cout << "Serialized scene:\n" << s << std::endl;
    // ren::loadObj("assets/test/meshes/unit_cube.obj");
    // ren::loadGLTF("assets/test/meshes/unit_cube.glb");
    // ren::loadGLTF("assets/test/meshes/suzanne.glb");
    // exit(0);
  }

  Application::~Application() {
    REN_PROFILE_FUNCTION();
    this->renderer->waitForIdle();

    // Clear the layer stack
    this->layerStack.clear();

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

    // ---------------------------------------------------

    // Okay. lets make a simple g buffer opaque pass.
    RenderPass::Description renderPassDesc;
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
    auto renderPass = makeRef<RenderPass>(renderPassDesc);

    // now make a render target for the gbuffer.
    auto target = renderPass->createRenderTarget(512, 512);
    fmt::println("Created render target {}", (u64)target->getHandle());
    // Make an empty descriptor set layout for the gbuffer because for now we don't have any textures or samplers.

    // TODO: abstract this crap. (Maybe reflect it from the shaders.)
    VkDescriptorSetLayout gbufferLayout = VK_NULL_HANDLE;
    {
      REN_PROFILE_SCOPE("Create GBuffer Descriptor Set Layout");

      VkDescriptorSetLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
      layoutInfo.bindingCount = 0;
      layoutInfo.pBindings = nullptr;

      if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, nullptr,
                                      &gbufferLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
      }
    }

    // but, to make those buffers, we need to make a render pipeline!
    auto gbufferPipeline = StandardPipeline(
      renderPass,
      makeRef<VertexShader>("shaders/opaque.vert.spv"),
      makeRef<FragmentShader>("shaders/opaque.frag.spv"),
      gbufferLayout
    );

    // Now we have a deferred rendering pipeline.


    // Make a simple triangle to render in screen space.
    std::vector<Vertex> vertices = {
        Vertex(glm::vec3(-0.5f, -0.5f, 0.0f)),
        Vertex(glm::vec3(-0.5f,  0.5f, 0.0f)),
        Vertex(glm::vec3( 0.5f,  0.5f, 0.0f)),
    };
    std::vector<u32> indices = {0, 1, 2};

    auto vertexBuffer = VertexBuffer(vertices);
    auto indexBuffer = IndexBuffer(indices);

    auto render_test = [&]() {
      REN_PROFILE_SCOPE("My Render Test");


      renderer->withPass(*renderPass, *target, [&]() {
        REN_PROFILE_SCOPE("Render Test Pass");
        auto cmd = ren::getFrameData().commandBuffer;
        gbufferPipeline.bind(cmd);

        ren::bind(cmd, vertexBuffer);
        ren::bind(cmd, indexBuffer);
        // draw a single triangle on the screen.
        vkCmdDrawIndexed(cmd, indices.size(), 1, 0, 0, 0);
      });
    };

    // ---------------------------------------------------



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


      if (0) {
        REN_PROFILE_SCOPE("Render Graph Setup");
        ren::RenderGraph graph;
        auto shadowMap = graph.addNode("ShadowMap");
        shadowMap->addOutput("shadowMap");


        auto gbuffer = graph.addNode("Gbuffer");
        gbuffer->addOutput("emissive");
        gbuffer->addOutput("albedo");
        gbuffer->addOutput("normal");
        gbuffer->addOutput("pbr");
        gbuffer->addOutput("depthStencil");

        auto lighting = graph.addNode("Lighting");
        lighting->addInput("emissive");
        lighting->addInput("albedo");
        lighting->addInput("normal");
        lighting->addInput("pbr");
        lighting->addInput("depthStencil");
        lighting->addInput("shadowMap");
        lighting->addOutput("HDR");

        auto tonemap = graph.addNode("Tonemap");
        tonemap->addInput("HDR");
        tonemap->addInput("shadowMap");
        tonemap->addOutput("LDR");

        auto present = graph.addNode("PresentAndUI");
        present->addInput("LDR");
        present->addOutput("SwapchainImage");

        {
          REN_PROFILE_SCOPE("Graph Run");
          graph.run();
        }

        // graph.dump();
        // exit(0);
      }


      render_test();
      // VkPhysicalDeviceProperties deviceProps;
      // vkGetPhysicalDeviceProperties(vulkan.physical_device, &deviceProps);
      // auto timestamps = frame.getQueryResults();
      // uint64_t ticksDiff = timestamps[1] - timestamps[0];

      // // Convert to nanoseconds
      // double nanoseconds = (double)ticksDiff * deviceProps.limits.timestampPeriod;

      // // Convert to milliseconds
      // double milliseconds = nanoseconds / 1000000.0;

      // fmt::println("Frame {}: {} ms", frame.frameIndex, milliseconds);
      // REN_PROFILE_RECORD_GPUTIME("GPU Pipeline", milliseconds);
      // REN_PROFILE_COUNTER("Pipeline", milliseconds);

      // vkCmdResetQueryPool(frame.commandBuffer, frame.queryPool, 0, frame.query_count);
      // vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      // frame.queryPool, 0);

      // Render the scene.

      // Render with ImGui.
      renderer->withPass(renderer->getRenderPass(), *frame.renderTarget, [&]() {
        REN_PROFILE_SCOPE("ImGui Render");
        {
          REN_PROFILE_SCOPE("ImGui New Frame");
          ImGui_ImplVulkan_NewFrame();
          ImGui_ImplSDL2_NewFrame();

          ImGui::NewFrame();
        }

        

        layerStack.onImGuiRender(deltaTime);

        {
          REN_PROFILE_SCOPE("ImGui Render Draw Data");
          ImGui::Render();
          ImGui::UpdatePlatformWindows();
          ImGui::RenderPlatformWindowsDefault();
          // Gross leakage.
          ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ren::getFrameData().commandBuffer);
        }
      });


      renderer->endFrame();



      // Update the layers.
      layerStack.onUpdate(deltaTime);
    }
  }


}  // namespace ren