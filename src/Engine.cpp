#include <ren/Engine.h>

#include <stb/stb_image.h>


#include <imgui_impl_sdl2.h>
#include "imgui.h"
#include "vulkan/vulkan_core.h"

ren::Engine::Engine(const std::string& app_name, glm::uvec2 window_size)
    : app_name(app_name) {
  // First, initialize SDL with video and vulkan support.
  SDL_Init(SDL_INIT_VIDEO);
  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  // Then, create a window with the specified name and size.
  this->window =
      SDL_CreateWindow(app_name.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       window_size.x, window_size.y, window_flags);

  // Now, we can get going with Vulkan.
  // First step is to create a Vulkan instance.

  this->vulkan = std::make_unique<ren::VulkanInstance>(app_name, this->window);
}



struct Camera {
  glm::vec3 position = {0.0f, 0.0f, 3.0f};
  glm::vec3 angles = {0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
  bool mouse_captured = false;
  bool first_update = true;
  float cameraSpeed = 1.0f;

  glm::mat4 view_matrix() const {
    float pitch = angles.x;
    float yaw = angles.y;

    float sinPitch = sinf(pitch);
    float cosPitch = cosf(pitch);
    float sinYaw = sinf(yaw);
    float cosYaw = cosf(yaw);

    glm::vec3 forward(sinYaw * cosPitch,  // X component
                      sinPitch,           // Y component
                      -cosYaw * cosPitch  // Z component
    );

    glm::mat4 view = glm::lookAt(position, position + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    return view;
  }

  void update(float dt) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    int mouse_x, mouse_y;
    SDL_GetRelativeMouseState(&mouse_x, &mouse_y);

    if (first_update) {
      // Capture mouse on first update
      SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
      first_update = false;
    }

    // Toggle mouse capture with ESC
    if (keys[SDL_SCANCODE_E]) {
      mouse_captured = !mouse_captured;
      SDL_SetRelativeMouseMode(mouse_captured ? SDL_TRUE : SDL_FALSE);
      SDL_Delay(100);  // Prevent rapid toggling
    }

    if (!mouse_captured) return;

    // Mouse look
    angles.y += mouse_x * 0.002f;  // yaw
    angles.x -= mouse_y * 0.002f;  // pitch

    // Clamp pitch
    if (angles.x > M_PI / 2.0f) angles.x = M_PI / 2.0f;
    if (angles.x < -M_PI / 2.0f) angles.x = -M_PI / 2.0f;

    // Movement vectors
    float cos_yaw = cosf(angles.y);
    float sin_yaw = sinf(angles.y);
    glm::vec3 forward = {sin_yaw, 0.0f, -cos_yaw};
    glm::vec3 right = {cos_yaw, 0.0f, sin_yaw};

    float speed = cameraSpeed * dt;

    // WASD movement
    if (keys[SDL_SCANCODE_W]) {
      // position.x += speed / 2.0f;
      position.x += forward.x * speed;
      position.z += forward.z * speed;
    }
    if (keys[SDL_SCANCODE_S]) {
      // position.x -= speed / 2.0f;
      position.x -= forward.x * speed;
      position.z -= forward.z * speed;
    }
    if (keys[SDL_SCANCODE_A]) {
      // position.z -= speed / 2.0f;
      position.x -= right.x * speed;
      position.z -= right.z * speed;
    }
    if (keys[SDL_SCANCODE_D]) {
      // position.z += speed / 2.0f;
      position.x += right.x * speed;
      position.z += right.z * speed;
    }
    if (keys[SDL_SCANCODE_SPACE]) { position.y += speed; }
    if (keys[SDL_SCANCODE_LSHIFT]) { position.y -= speed; }
  }
};



std::vector<ren::Vertex> vertices = {
    //
    {
        glm::vec3(-0.5f, -0.5f, 0.0f),
        glm::vec3(1.0f, 0.0f, 0.0f),
        -glm::vec2(1.0f, 0.0f),
    },
    {
        glm::vec3(0.5f, -0.5f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        -glm::vec2(0.0f, 0.0f),
    },
    {
        glm::vec3(0.5f, 0.5f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        -glm::vec2(0.0f, 1.0f),
    },
    {
        glm::vec3(-0.5f, 0.5f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        -glm::vec2(1.0f, 1.0f),
    },
};

const std::vector<uint32_t> indices = {
    0, 1, 2, 2, 3, 0,  // square 1
};




void ren::Engine::run(void) {
  SDL_Event e;
  bool bQuit = false;


  Camera camera;


  auto pipeline = std::make_shared<ren::VulkanPipeline>(*vulkan);


  auto bindingDesc = ren::Vertex::get_binding_description();
  auto attributeDescs = ren::Vertex::get_attribute_descriptions();


  // this is still a little gross.
  pipeline->vertexInputInfo.vertexBindingDescriptionCount = 1;
  pipeline->vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
  pipeline->vertexInputInfo.vertexAttributeDescriptionCount = attributeDescs.size();
  pipeline->vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();


  // set topology
  pipeline->inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipeline->rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  pipeline->depthStencil.depthTestEnable = VK_TRUE;
  pipeline->depthStencil.depthWriteEnable = VK_TRUE;



  std::shared_ptr<ren::VertexShader> vertex_shader =
      std::make_shared<ren::VertexShader>(*vulkan, "shaders/triangle.vert.spv");
  std::shared_ptr<ren::FragmentShader> fragment_shader =
      std::make_shared<ren::FragmentShader>(*vulkan, "shaders/triangle.frag.spv");


  pipeline->addShader(vertex_shader);
  pipeline->addShader(fragment_shader);

  VkDescriptorSetLayoutBinding uboLayoutBinding{};
  uboLayoutBinding.binding = 0;
  uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboLayoutBinding.descriptorCount = 1;
  uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  uboLayoutBinding.pImmutableSamplers = nullptr;  // Optional

  pipeline->addBinding(uboLayoutBinding);

  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 1;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.pImmutableSamplers = nullptr;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  pipeline->addBinding(samplerLayoutBinding);


  pipeline->build();

  auto vertex_buffer = std::make_shared<ren::VertexBuffer<ren::Vertex>>(*vulkan, vertices);
  auto index_buffer = std::make_shared<ren::IndexBuffer>(*vulkan, indices);



  auto startTime = std::chrono::high_resolution_clock::now();
  auto lastTime = startTime;


  float fov = 90.0f;
  float fNear = 0.01f;
  float fFar = 1000.0f;

  int drawCount = 10;
  // main loop
  while (!bQuit) {
    // Handle events on queue
    while (SDL_PollEvent(&e) != 0) {
      if (!camera.mouse_captured) { ImGui_ImplSDL2_ProcessEvent(&e); }

      // if the window resizes
      if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_RESIZED) {
        int width = e.window.data1;
        int height = e.window.data2;
        fmt::print("Window resized to {}x{}\n", width, height);
        // Update the Vulkan swapchain with the new size
        this->vulkan->framebuffer_resized = true;
      }

      // close the window when user alt-f4s or clicks the X button
      if (e.type == SDL_QUIT) {
        bQuit = true;
      } else if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_SPACE) {}
      }
    }



    auto cb = vulkan->beginFrame();
    if (cb == VK_NULL_HANDLE) {
      fmt::print("Failed to begin frame, skipping...\n");
      continue;
    }


    auto currentTime = std::chrono::high_resolution_clock::now();
    float time =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto deltaTime =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
    lastTime = currentTime;


    camera.update(deltaTime);


    ImGui::Begin("Test Controls");
    ImGui::Text("FPS: %.2f", 1.0f / deltaTime);
    ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 180.0f);
    ImGui::DragFloat("Near Plane", &fNear, 0.01f, 0.001f, 10.0f);
    ImGui::DragFloat("Far Plane", &fFar, 0.1f, 1.0f, 10000.0f);
    ImGui::DragInt("Draw Count", &drawCount, 1, 1);
    ImGui::DragFloat("Camera Speed", &camera.cameraSpeed, 0.1f, 0.1f);
    ImGui::End();
    // vulkan->record_command_buffer(cb, vulkan->imageIndex);

    auto matProj = glm::perspective(
        glm::radians(fov), vulkan->extent.width / (float)vulkan->extent.height, fNear, fFar);
    matProj[1][1] *= -1;


    auto matView = camera.view_matrix();

    vulkan->update_uniform_buffer(vulkan->imageIndex);

    ren::bind(cb, *pipeline);


    for (int i = 0; i < drawCount; i++) {
      ren::bind(cb, *vertex_buffer);
      ren::bind(cb, *index_buffer);


      auto dset = pipeline->getDescriptorSet(vulkan->imageIndex);
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getLayout(), 0, 1,
                              &dset, 0, nullptr);


      MeshPushConstants push_constants{};

      float loc = i / 5.0f;
      glm::vec3 transform(sin(loc), cos(loc), 1.0f - (i * 0.001f));

      push_constants.model = glm::scale(glm::translate(glm::mat4(1.0), transform), glm::vec3(1.0)) *
                             glm::rotate(glm::mat4(1.0f), 0 * glm::radians(90.0f),
                                         glm::vec3(0.0f, 0.0f, 1.0f));

      push_constants.view = matView;
      push_constants.proj = matProj;


      vkCmdPushConstants(cb, pipeline->getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                         sizeof(push_constants), &push_constants);

      vkCmdDrawIndexed(cb, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }

    vulkan->endFrame();
  }
  // Before exting, we need to wait for the device to finish all operations.
  vkDeviceWaitIdle(vulkan->device);
}
