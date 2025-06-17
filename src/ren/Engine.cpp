#include <ren/Engine.h>

#include <stb/stb_image.h>
#include <ren/renderer/pipelines/StandardPipeline.h>
#include <ren/renderer/Texture.h>
#include <ren/Camera.h>

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


void generate_sphere(std::vector<ren::Vertex>& vertices, std::vector<uint32_t>& indices,
                     float radius = 1.0f, uint32_t segments = 5, uint32_t rings = 8,
                     glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f)) {
  vertices.clear();
  indices.clear();

  // Generate vertices
  for (uint32_t ring = 0; ring <= rings; ring++) {
    float phi = M_PI * ring / rings;  // Latitude angle (0 to π)
    float y = radius * cos(phi);
    float ring_radius = radius * sin(phi);

    for (uint32_t segment = 0; segment <= segments; segment++) {
      float theta = 2.0f * M_PI * segment / segments;  // Longitude angle (0 to 2π)

      ren::Vertex vertex;

      // Position
      vertex.pos.x = ring_radius * cos(theta);
      vertex.pos.y = y;
      vertex.pos.z = ring_radius * sin(theta);

      // Color (you can vary this based on position if desired)
      vertex.color = vertex.pos;

      // Texture coordinates
      vertex.texCoord.x = (float)segment / segments;
      vertex.texCoord.y = (float)ring / rings;

      vertices.push_back(vertex);
    }
  }

  // Generate indices for triangles
  for (uint32_t ring = 0; ring < rings; ring++) {
    for (uint32_t segment = 0; segment < segments; segment++) {
      uint32_t current = ring * (segments + 1) + segment;
      uint32_t next = current + segments + 1;

      indices.push_back(current);
      indices.push_back(current + 1);
      indices.push_back(next);

      indices.push_back(current + 1);
      indices.push_back(next + 1);
      indices.push_back(next);
    }
  }
}




void ren::Engine::run(void) {
  SDL_Event e;
  bool bQuit = false;


  ren::Camera camera;

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  generate_sphere(vertices, indices, 1.0f, 64, 64, glm::vec3(1.0f, 1.0f, 1.0f));



  auto vertex_shader =
      std::make_shared<ren::Shader>("shaders/triangle.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
  auto fragment_shader =
      std::make_shared<ren::Shader>("shaders/triangle.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

  auto bindingDesc = ren::Vertex::get_binding_description();
  auto attributeDescs = ren::Vertex::get_attribute_descriptions();

  // First, allocate the descriptor set layout.
  VkDescriptorSetLayout descriptorSetLayout;

  std::vector<VkDescriptorSetLayoutBinding> bindings;

  VkDescriptorSetLayoutBinding samplerLayoutBinding{};
  samplerLayoutBinding.binding = 0;
  samplerLayoutBinding.descriptorCount = 1;
  samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerLayoutBinding.pImmutableSamplers = nullptr;
  samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(samplerLayoutBinding);


  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();

  if (vkCreateDescriptorSetLayout(vulkan->device, &layoutInfo, nullptr, &descriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }


  ren::StandardPipeline pipeline(vertex_shader, fragment_shader, descriptorSetLayout);


  // now that we have a pipeline, we can create a descriptor pool

  std::array<VkDescriptorPool, MAX_FRAMES_IN_FLIGHT> descriptorPools;

  for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    std::vector<VkDescriptorPoolSize> poolSizes;
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000});
    poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000});

    // create the descriptor pool
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1000;  // Maximum number of descriptor sets we can
    if (vkCreateDescriptorPool(vulkan->device, &poolInfo, nullptr, &descriptorPools[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor pool!");
    }
  }



  VkDescriptorPool materialPool;
  std::vector<VkDescriptorPoolSize> poolSizes;
  poolSizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000});

  // create the descriptor pool
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = 1000;  // Maximum number of descriptor sets we can
  if (vkCreateDescriptorPool(vulkan->device, &poolInfo, nullptr, &materialPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }




  // Load our textures.
  auto mars_tex = ren::Texture::load("assets/mars.jpg");
  auto moon_tex = ren::Texture::load("assets/moon.jpg");

  auto makeMaterialSet = [&](std::shared_ptr<ren::Texture> texture) {
    VkDescriptorSet descriptorSet;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = materialPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(vulkan->device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate descriptor set!");
    }

    // Update the descriptor set with the texture
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture->getImageView();
    imageInfo.sampler = texture->getSampler();

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(vulkan->device, 1, &descriptorWrite, 0, nullptr);

    return descriptorSet;
  };

  struct Body {
    glm::vec3 transform;
    std::shared_ptr<ren::Texture> texture;
    VkDescriptorSet materialSet;
  };


  std::vector<Body> bodies;

  bodies.push_back(Body{glm::vec3(0.0f, 0.0f, 0.0f), mars_tex,
                        makeMaterialSet(mars_tex)});  // Center body (e.g., Mars)
  bodies.push_back(Body{glm::vec3(4.0f, 0.0f, 0.0f), moon_tex,
                        makeMaterialSet(moon_tex)});  // Orbiting body (e.g., Moon)


  auto vertex_buffer = std::make_shared<ren::VertexBuffer<ren::Vertex>>(*vulkan, vertices);
  vertex_buffer->setName("Planet Vertex Buffer");
  auto index_buffer = std::make_shared<ren::IndexBuffer>(*vulkan, indices);
  index_buffer->setName("Planet Index Buffer");



  auto startTime = std::chrono::high_resolution_clock::now();
  auto lastTime = startTime;


  float fov = 90.0f;
  float fNear = 0.01f;
  float fFar = 1000.0f;
  float radius = 2.0f;

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


    VkDescriptorPool descriptorPool = descriptorPools[vulkan->imageIndex];
    vkResetDescriptorPool(vulkan->device, descriptorPool, 0);


    auto currentTime = std::chrono::high_resolution_clock::now();
    float time =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto deltaTime =
        std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
    lastTime = currentTime;


    camera.update(deltaTime);
    // nbody_step(bodies, deltaTime * simulationSpeed);


    ImGui::Begin("Test Controls");
    ImGui::Text("FPS: %.2f", 1.0f / deltaTime);
    ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 180.0f);
    ImGui::DragFloat("Near Plane", &fNear, 0.01f, 0.001f, 10.0f);
    ImGui::DragFloat("Far Plane", &fFar, 0.1f, 1.0f, 10000.0f);
    ImGui::DragFloat("Camera Speed", &camera.cameraSpeed, 0.1f, 0.1f);

    ImGui::DragFloat3("Camera Position", &camera.position.x, 0.1f);
    ImGui::DragFloat3("Camera Velocity", &camera.velocity.x, 0.1f);
    ImGui::End();
    // vulkan->record_command_buffer(cb, vulkan->imageIndex);

    auto matProj = glm::perspective(
        glm::radians(fov), vulkan->extent.width / (float)vulkan->extent.height, fNear, fFar);
    matProj[1][1] *= -1;


    auto matView = camera.view_matrix();

    ren::bind(cb, pipeline);
    ren::bind(cb, *vertex_buffer);
    ren::bind(cb, *index_buffer);



    const double golden_angle = M_PI * (3.0 - sqrt(5.0));  // ~2.399... radians



    for (size_t i = 0; i < bodies.size(); ++i) {
      auto& body = bodies[i];
      // allocate a descriptor set for this body from descriptorPool
      VkDescriptorSet descriptorSet;
      VkDescriptorSetAllocateInfo allocInfo{};
      allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      allocInfo.descriptorPool = descriptorPool;
      allocInfo.descriptorSetCount = 1;
      allocInfo.pSetLayouts = &descriptorSetLayout;

      if (vkAllocateDescriptorSets(vulkan->device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set!");
      }

      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.getLayout(), 0, 1,
                              &body.materialSet, 0, nullptr);


      const auto& pos = body.transform;
      MeshPushConstants push_constants{};

      glm::vec3 transform = pos;
      push_constants.model = glm::translate(glm::mat4(1.0), transform);
      push_constants.view = matView;
      push_constants.proj = matProj;


      vkCmdPushConstants(cb, pipeline.getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                         sizeof(push_constants), &push_constants);

      vkCmdDrawIndexed(cb, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }

    vulkan->endFrame();
  }
  // Before exting, we need to wait for the device to finish all operations.
  vkDeviceWaitIdle(vulkan->device);
}
