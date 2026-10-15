#include <ren/renderer/Viewport.h>

#include <ren/renderer/Renderer.h>
#include <ren/renderer/Swapchain.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/shader/ShaderCache.h>
#include <ren/renderer/submission/FrameContext.h>
#include <ren/core/Application.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <SDL3/SDL_keyboard.h>
#include <ren/renderer/graph/tasks/GBufferTask.h>
#include <ren/core/Flag.h>

static ren::Flag<std::string> skyBoxPath("skybox", "assets/hdri/sky.jpg", "Path to the skybox texture to use in the viewport.");

namespace ren {

  Viewport::Viewport() {
    // The graph is built once. Rebuilding it per frame would mean re-adding
    // every task (RenderGraph has no removal path) and paying the compile()
    // cost, which is already flagged as slow.
    buildGraph();
  }


  void Viewport::buildGraph() {
    ren::GraphHandle nullHandleOut;
    ren::GraphHandle ssao;
    ren::GraphHandle gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth;
    auto &gbp = ren::addGBuffer(graph, gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth);




    forwardPSO.debugName = "ViewportForward";
    forwardPSO.program = ren::getShader("demo/viewport_clear");
    // The fullscreen triangle is generated from SV_VertexID, so there is no
    // vertex buffer to bind, nothing to cull, and no depth to test.
    forwardPSO.hasVertexBinding = false;
    forwardPSO.cullMode = ren::CullMode::None;
    forwardPSO.depthTest = false;
    forwardPSO.depthWrite = false;

    auto skybox = ren::Texture::load(skyBoxPath.get(), VK_FORMAT_R8G8B8A8_SRGB);
    ren::println("Viewport: skybox texture {}", skybox->getName());

    // The sky texture is an LDR JPEG. The shader samples its values into a
    // SRGB target and uses FrameGlobals to orient the sky and grid.
    graph.pass("forward")
        .createColorAttachment("viewportColor", {.relativeScale = glm::vec2(1.0f), .format = VK_FORMAT_R8G8B8A8_SRGB}, finalImage)
        .reads(gbufferAlbedo, GraphAccess::ShaderRead)
        .render([this, skybox](GraphRenderPassContext &ctx) {
          auto shader = ctx.encoder.bindGraphics(forwardPSO);
          auto root = shader.root("pc");
          root.set("skyboxHdri", skybox);
          // root.set("raysPerPixel", raysPerPixel);
          // root.set("focusDistance", focusDistance);
          // root.set("lensRadius", lensRadius);
          shader.drawFullscreenTriangle();
        });
  }


  void Viewport::update(float dt, glm::uvec2 size) {
    // Clamp render size
    renderSize = glm::max(size, glm::uvec2(1, 1));

    // call out to subclass for controls and per-frame logic.
    onUpdate(dt);

    // Prepare the graph for the frame.
    graph.startFrame(renderSize);
    prepared = true;
  }


  ImageRef Viewport::getFinalImage() const {
    if (!prepared || finalImage == nullGraphHandle) {
      return nullptr;
    }
    return graph.getImage(finalImage);
  }


  VkDescriptorSet Viewport::getImGuiTextureID() const {
    if (!prepared || finalImage == nullGraphHandle) {
      return VK_NULL_HANDLE;
    }

    auto resource = graph.get<ImageResource>(finalImage);
    if (!resource->image) {
      return VK_NULL_HANDLE;
    }

    // Created lazily and cached on the resource itself, which is also what
    // frees it: buildImage() drops the descriptor set whenever it reallocates,
    // so this re-creates it against the new image view on the next frame.
    if (resource->imguiTextureID == VK_NULL_HANDLE) {
      resource->imguiTextureID =
          ImGui_ImplVulkan_AddTexture(resource->sampler->getHandle(), resource->image->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    return resource->imguiTextureID;
  }


  void Viewport::render(Renderer &renderer) {
    if (!prepared) {
      return;  // update() has not run, so the graph has no images yet.
    }

    auto &frame = ren::getFrameUnit();
    auto encoder = frame.getMainCommandEncoder();

    // Allocate and initialize the frame globals buffer.
    auto *globalsBuffer = frame.getArena().push<UniformBuffer<FrameGlobals>>(1);

    const glm::vec2 sizeF(renderSize);
    globalsBuffer->hostData()[0] = FrameGlobals{
        .time = static_cast<float>(ren::Application::get().timeSeconds),
        .deltaTime = ren::deltaTime(),
        .frameNumber = static_cast<u32>(ren::getVulkan().frame_number),
        .renderSize = sizeF,
        .inverseRenderSize = 1.0f / sizeF,
        .viewMatrix = view,
        .projMatrix = projection,
    };

    VkDescriptorSet globalsSet = VK_NULL_HANDLE;
    if (!frame.getDescriptorAllocator().allocate(&globalsSet, renderer.getFrameGlobalsAllocator().getLayout())) {
      throw std::runtime_error("Viewport: failed to allocate frame globals descriptor set");
    }

    VkDescriptorBufferInfo bufferInfo{
        .buffer = globalsBuffer->getHandle(),
        .offset = 0,
        .range = sizeof(FrameGlobals),
    };
    VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = globalsSet,
        .dstBinding = ShaderABI::frameBinding,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &bufferInfo,
    };
    vkUpdateDescriptorSets(getVulkan().device, 1, &write, 0, nullptr);

    encoder->setFrameGlobals(globalsSet);

    try {
      graph.run(renderer);
    } catch (const std::exception &e) {
      ren::println("✗ Viewport render graph failed: {}", e.what());
    }

    // Hand set 0 back to the frame's own globals, so anything drawn outside a
    // viewport (the display pass, ImGui, debug lines) is unaffected by this.
    encoder->setFrameGlobals(VK_NULL_HANDLE);
  }


  void Viewport::inspect(void) { graph.inspect(); }


  // ------------------------------------------------------------------ //

  bool EditorViewport::drawWindow(const char *title, float dt, bool *open) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool visible = ImGui::Begin(title, open, ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleVar();  // Popped right after Begin so nested widgets are unaffected.

    if (!visible) {
      // Collapsed or clipped: there is nothing to size against, and rendering
      // into a zero-area window would be wasted work. Leave the graph at
      // whatever size it last had and skip the update entirely.
      ImGui::End();
      return open ? *open : true;
    }

    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Show Input Overlay", nullptr, &showInputOverlay);
        ImGui::MenuItem("Show Graph Inspector", nullptr, &showGraphInspector);
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 extent(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f));

    // Capture mouse state relative to the viewport image. An InvisibleButton
    // over the whole region gives us hover/active state that respects ImGui's
    // window stacking, so widgets drawn on top still win the mouse -- and so
    // that sibling viewports never both claim the same drag.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##viewportInput", extent,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    auto &io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const ImVec2 local(mouse.x - origin.x, mouse.y - origin.y);

    input.hovered = ImGui::IsItemHovered();
    input.active = ImGui::IsItemActive();
    // "Has input" means hovered *or* actively dragged: a look that starts
    // inside the viewport must keep working once the cursor leaves it.
    const bool hasInput = input.hovered || input.active;

    input.leftDown = hasInput && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    input.rightDown = hasInput && ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.middleDown = hasInput && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.mousePixels = glm::vec2(local.x, local.y);
    input.mouseNormalized = glm::vec2(extent.x > 0 ? local.x / extent.x : 0.0f, extent.y > 0 ? local.y / extent.y : 0.0f);
    input.mouseDelta = hasInput ? glm::vec2(io.MouseDelta.x, io.MouseDelta.y) : glm::vec2(0.0f);
    input.wheel = input.hovered ? io.MouseWheel : 0.0f;

    // A right-drag that began inside this viewport captures the look, and
    // holds it until the button comes up -- wherever the cursor has wandered.
    if (!looking && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
      looking = true;
    } else if (looking && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
      looking = false;
    }

    // Update the viewport's graph
    update(dt, glm::uvec2(extent.x * renderScale, extent.y * renderScale));

    // Rewind to the top-left and draw the rendered image over the button. The
    // image is filled in later this frame, during the render phase.
    ImGui::SetCursorScreenPos(origin);
    if (auto texture = getImGuiTextureID()) {
      ImGui::Image((ImTextureID)texture, extent);
    }

    if ((showInputOverlay || overlayContent) && extent.x > 16.0f && extent.y > 16.0f) {
      constexpr float margin = 8.0f;
      const float width = std::min(360.0f, extent.x - 2.0f * margin);
      const float height = std::min(320.0f, extent.y - 2.0f * margin);

      ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, origin.y + margin));
      ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, height));
      // ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
      if (ImGui::BeginChild("##viewportOverlay", ImVec2(width, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        if (ImGui::CollapsingHeader("Viewport Overlay", ImGuiTreeNodeFlags_DefaultOpen)) {
          if (showInputOverlay) {
            const auto text = std::format(
                "hovered: {}  active: {}  looking: {}\n"
                "L:{} R:{} M:{}\n"
                "px: {:.0f}, {:.0f}\n"
                "norm: {:.3f}, {:.3f}\n"
                "delta: {:.1f}, {:.1f}  wheel: {:.2f}\n"
                "size: {} x {}\n"
                "pos: {:.2f}, {:.2f}, {:.2f}",
                input.hovered, input.active, looking, input.leftDown, input.rightDown, input.middleDown, input.mousePixels.x, input.mousePixels.y,
                input.mouseNormalized.x, input.mouseNormalized.y, input.mouseDelta.x, input.mouseDelta.y, input.wheel, getRenderSize().x,
                getRenderSize().y, position.x, position.y, position.z);
            ImGui::TextWrapped("%s", text.c_str());
          }
          if (overlayContent) {
            overlayContent();
          }

          ImGui::DragInt("raysPerPixel", &raysPerPixel, 1.0f, 1, 64);
          ImGui::DragFloat("focusDistance", &focusDistance, 0.1f, 0.1f, 100.0f);
          ImGui::DragFloat("lensRadius", &lensRadius, 0.01f, 0.0f, 1.0f);
          ImGui::DragFloat("renderScale", &renderScale, 0.01f, 0.1f, 1.0f);
        }
      }
      ImGui::EndChild();
      ImGui::PopStyleVar();
      // ImGui::PopStyleColor();
    }

    ImGui::End();


    if (showGraphInspector) {
      ImGui::Begin("Graph Inspector", &showGraphInspector);
      this->graph.inspect();
      ImGui::End();
    }

    return open ? *open : true;
  }


  void EditorViewport::onUpdate(float dt) {
    const glm::uvec2 size = getRenderSize();
    const float aspect = static_cast<float>(size.x) / static_cast<float>(size.y);

    if (looking) {
      angles.y += input.mouseDelta.x * lookSensitivity;  // yaw
      angles.x -= input.mouseDelta.y * lookSensitivity;  // pitch

      constexpr float limit = glm::half_pi<float>() - 0.001f;
      angles.x = glm::clamp(angles.x, -limit, limit);
    }

    const float cosYaw = std::cos(angles.y);
    const float sinYaw = std::sin(angles.y);
    const glm::vec3 forwardFlat(sinYaw, 0.0f, -cosYaw);
    const glm::vec3 right(cosYaw, 0.0f, sinYaw);


    if (looking) {
      const bool *keys = SDL_GetKeyboardState(nullptr);
      glm::vec3 impulse(0.0f);
      const float speed = moveSpeed * dt;

      if (keys[SDL_SCANCODE_W]) {
        impulse += forwardFlat * speed;
      }
      if (keys[SDL_SCANCODE_S]) {
        impulse -= forwardFlat * speed;
      }
      if (keys[SDL_SCANCODE_A]) {
        impulse -= right * speed;
      }
      if (keys[SDL_SCANCODE_D]) {
        impulse += right * speed;
      }
      if (keys[SDL_SCANCODE_SPACE]) {
        impulse.y += speed;
      }
      if (keys[SDL_SCANCODE_LSHIFT]) {
        impulse.y -= speed;
      }

      velocity += impulse;
    }

    // Maximum move speed
    if (glm::length(velocity) > moveSpeed) {
      velocity = glm::normalize(velocity) * moveSpeed;
    }

    position += velocity * dt;
    // Damping factor
    velocity *= std::max(0.0f, 1.0f - dt * 5.0f);

    const float pitch = angles.x;
    const glm::vec3 forward(sinYaw * std::cos(pitch), std::sin(pitch), -cosYaw * std::cos(pitch));

    view = glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));

    // Not Camera::projectionMatrix(), because that hardcodes the FOV and
    // planes as statics; a viewport exposes them per instance.
    projection = glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
    projection[1][1] *= -1;  // Vulkan's clip space has Y pointing down.
  }

}  // namespace ren
