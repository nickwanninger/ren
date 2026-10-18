#include <ren/renderer/Viewport.h>

#include <ren/renderer/Renderer.h>
#include <ren/renderer/Swapchain.h>
#include <ren/renderer/CommandEncoder.h>
#include <ren/renderer/shader/ShaderCache.h>
#include <ren/renderer/submission/FrameContext.h>
#include <ren/core/Application.h>

#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_vulkan.h>
#include <ImGuizmo/ImGuizmo.h>
#include <SDL3/SDL_keyboard.h>
#include <ren/renderer/graph/tasks/GBufferTask.h>
#include <ren/renderer/graph/tasks/SSAOTask.h>
#include <ren/core/Flag.h>

static ren::Flag<std::string> skyBoxPath("skybox", "assets/hdri/sky.jpg", "Path to the skybox texture to use in the viewport.");

namespace ren {

  // The graph is not built here: a virtual call in a constructor dispatches to
  // Viewport::buildGraph(), never to a subclass override. update() builds it.
  Viewport::Viewport() {}


  void Viewport::buildGraph() {
    ren::GraphHandle nullHandleOut;
    ren::GraphHandle ssao;
    ren::GraphHandle gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth;
    auto &gbp = ren::addGBuffer(graph, gbufferAlbedo, gbufferNormal, gbufferMaterial, gbufferDepth);
    ren::addSSAO(graph, gbufferDepth, gbufferNormal, ssao);




    forwardPSO.debugName = "ViewportForward";
    // forwardPSO.program = ren::getShader("demo/viewport_clear");
    forwardPSO.program = ren::getShader("demo/viewport_clear", "ren/pass/fullscreen");
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
    graph.pass("viewport")
        .createColorAttachment("viewportColor", {.relativeScale = glm::vec2(1.0f), .format = VK_FORMAT_R8G8B8A8_SRGB}, finalImage)
        .reads(gbufferAlbedo, GraphAccess::ShaderRead)
        .reads(gbufferNormal, GraphAccess::ShaderRead)
        .reads(gbufferDepth, GraphAccess::ShaderRead)
        .reads(ssao, GraphAccess::ShaderRead)
        .render([this, skybox, gbufferAlbedo, gbufferNormal, gbufferDepth, ssao](GraphRenderPassContext &ctx) {
          auto shader = ctx.encoder.bindGraphics(forwardPSO);
          auto root = shader.root("root");
          root.set("skyboxHdri", skybox);
          // TEST lighting composite (see viewport_clear.slang).
          root.set("albedo", ctx.graph.get<ImageResource>(gbufferAlbedo)->textureHandle());
          root.set("normal", ctx.graph.get<ImageResource>(gbufferNormal)->textureHandle());
          root.set("depth", ctx.graph.get<ImageResource>(gbufferDepth)->textureHandle());
          root.set("ssao", ctx.graph.get<ImageResource>(ssao)->textureHandle());
          root.set("ssaoContrast", ssaoContrast);
          // root.set("raysPerPixel", raysPerPixel);
          // root.set("focusDistance", focusDistance);
          // root.set("lensRadius", lensRadius);
          shader.drawFullscreenTriangle();
        });
  }


  void Viewport::update(float dt, glm::uvec2 size) {
    // The graph is built once. Rebuilding it per frame would mean re-adding
    // every task (RenderGraph has no removal path) and paying the compile()
    // cost, which is already flagged as slow.
    if (!graphBuilt) {
      buildGraph();
      graphBuilt = true;
    }

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

    // Capture mouse state relative to the viewport image. This submits no
    // hoverable item: ImGuizmo does not start a drag while any item is hovered
    // or active. IsWindowHovered() still respects ImGui's window stacking, so
    // popups and the overlay child on top win the mouse.
    auto &io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const ImVec2 local(mouse.x - origin.x, mouse.y - origin.y);

    input.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + extent.x, origin.y + extent.y));

    // A drag that starts inside the viewport keeps input until its button
    // comes up, so that sibling viewports never both claim the same drag.
    for (int button = ImGuiMouseButton_Left; button <= ImGuiMouseButton_Middle; button++) {
      if (input.hovered && ImGui::IsMouseClicked(button)) {
        capturedButtons |= 1u << button;
      }
      if (!ImGui::IsMouseDown(button)) {
        capturedButtons &= ~(1u << button);
      }
    }
    input.active = capturedButtons != 0;
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
    // A view cube drag (left button) does not start a look. This reads the
    // gizmo state from the previous frame, because the cube is drawn later.
    if (!looking && !ImGuizmo::IsUsingViewManipulate() && input.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      looking = true;
    } else if (looking && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
      looking = false;
    }

    // Update the viewport's graph
    update(dt, glm::uvec2(extent.x * renderScale, extent.y * renderScale));

    // Draw the rendered image at the top-left. The image is filled in later
    // this frame, during the render phase.
    ImGui::SetCursorScreenPos(origin);
    if (auto texture = getImGuiTextureID()) {
      ImGui::Image((ImTextureID)texture, extent);
    }

    drawViewCube(glm::vec2(origin.x, origin.y), glm::vec2(extent.x, extent.y));

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
                "hovered: {}  active: {}  looking: {}, gizmo: {}\n"
                "L:{} R:{} M:{}\n"
                "px: {:.0f}, {:.0f}\n"
                "norm: {:.3f}, {:.3f}\n"
                "delta: {:.1f}, {:.1f}  wheel: {:.2f}\n"
                "size: {} x {}\n"
                "pos: {:.2f}, {:.2f}, {:.2f}",
                input.hovered, input.active, looking, ImGuizmo::IsUsingAny(), input.leftDown, input.rightDown, input.middleDown, input.mousePixels.x, input.mousePixels.y,
                input.mouseNormalized.x, input.mouseNormalized.y, input.mouseDelta.x, input.mouseDelta.y, input.wheel, getRenderSize().x,
                getRenderSize().y, position.x, position.y, position.z);
            ImGui::TextWrapped("%s", text.c_str());
          }
          if (overlayContent) {
            overlayContent();
          }

          // ImGui::SliderInt("raysPerPixel", &raysPerPixel, 1, 64);
          // ImGui::DragFloat("focusDistance", &focusDistance, 0.1f, 0.1f, 100.0f);
          // ImGui::DragFloat("lensRadius", &lensRadius, 0.01f, 0.0f, 1.0f);
          ImGui::DragFloat("renderScale", &renderScale, 0.01f, 0.1f, 2.0f);
          ImGui::DragFloat("ssaoContrast", &ssaoContrast, 0.01f, 0.1f, 10.0f);
          ImGui::SliderFloat("moveSpeed", &moveSpeed, 1.0f, 128.0f);
          ImGui::SliderFloat("Fov", &fovDegrees, 1.0f, 179.0f);
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


  void EditorViewport::drawViewCube(glm::vec2 origin, glm::vec2 extent) {
    constexpr float cubeSize = 128.0f;
    if (extent.x < cubeSize || extent.y < cubeSize) {
      return;
    }

    // ImGuizmo keeps one global context. Point it at this window's draw list,
    // so the cube draws over the image, and at this viewport's screen rect.
    ImGuizmo::PushID(this);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(origin.x, origin.y, extent.x, extent.y);

    #if 0
    // ImGuizmo expects GL clip space, so undo the Vulkan Y-flip.
    glm::mat4 gizmoProj = projection;
    gizmoProj[1][1] *= -1;

    glm::mat4 gizmoView = view;
    glm::mat4 identity(1.0f);
    ImGuizmo::ViewManipulate(glm::value_ptr(gizmoView), glm::value_ptr(gizmoProj), ImGuizmo::TRANSLATE, ImGuizmo::WORLD, glm::value_ptr(identity),
                             orbitDistance, ImVec2(origin.x + extent.x - cubeSize, origin.y), ImVec2(cubeSize, cubeSize), 0x10101010);

    // onUpdate() rebuilds the view from position and angles each frame, so
    // write the cube's change back into them. The camera has no roll.
    if (gizmoView != view) {
      const glm::mat4 inverse = glm::inverse(gizmoView);
      const glm::vec3 forward = -glm::vec3(inverse[2]);
      position = glm::vec3(inverse[3]);
      angles.x = std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
      angles.y = std::atan2(forward.x, -forward.z);
      velocity = glm::vec3(0.0f);
      view = gizmoView;
    }


    // ImGuizmo::DrawGrid(glm::value_ptr(view), glm::value_ptr(gizmoProj), glm::value_ptr(glm::mat4(1.0f)), 100.0f);
    static glm::mat4 matrix(1.0f);

    ImGuizmo::DrawCubes(glm::value_ptr(view), glm::value_ptr(gizmoProj), glm::value_ptr(matrix), 1);

    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProj), ImGuizmo::TRANSLATE | ImGuizmo::ROTATE | ImGuizmo::SCALE, ImGuizmo::MODE::WORLD, glm::value_ptr(matrix), NULL,
                         NULL);
    #endif


    ImGuizmo::PopID();
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
