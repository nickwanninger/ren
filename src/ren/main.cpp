#include <iostream>
#include <vector>

#include <ren/core/Instrumentation.h>
#include <ren/core/Application.h>

#include <ren/core/Components.h>
#include <flecs.h>

#include <type_traits>
#include <imgui.h>
#include <ren/core/AutoPlugin.h>

#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <ren/core/DebugLines.hpp>

struct CubeWave {};

static void test_plugin(ren::Application &app) {
  app.world.component<CubeWave>();

  app.world.system<ren::comp::Transform>("ren::core::TestSystem")
      .with<CubeWave>()
      .multi_threaded(true)
      .each([](ren::comp::Transform &t) {
        t.translation.y =
            sin(t.translation.x / 8.0f + t.translation.z / 8.0f + ren::timeSeconds() * 2.0f) * 2.0f;
      });
}

REN_PLUGIN("Test Plugin", test_plugin);




void dump_entity_bytes(flecs::entity e) {
  ecs_world_t *world = e.world();
  ecs_entity_t ent = e;  // implicit conversion

  // Access the entity's record (contains table + row index)
  ecs_record_t *rec = ecs_record_find(world, ent);
  if (!rec) return;

  const ecs_table_t *table = rec->table;
  if (!table) return;

  const ecs_type_t *type = ecs_table_get_type(table);
  int32_t row = ECS_RECORD_TO_ROW(rec->row);  // <-- row index

  for (int i = 0; i < type->count; i++) {
    ecs_entity_t comp_id = type->array[i];
    flecs::entity comp(world, comp_id);

    const ecs_type_info_t *ti = ecs_get_type_info(world, comp_id);
    if (!ti) {
      continue;  // skip tags, empty types
    }

    size_t size = ti->size;  // 0 for tags.

    void *ptr = ecs_table_get_column(table, i, row);
    std::cout << "Component: " << comp.name() << " (" << size << " bytes)\n";
    if (size == 0) continue;

    unsigned char *bytes = static_cast<unsigned char *>(ptr);
    for (size_t j = 0; j < size; j++) {
    std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[j])
                << " ";
    }
    std::cout << std::dec << "\n";
  }
}


struct DebugLineDraw {};


struct MyEvent {
  ren::Entity e;
  int value;
};


static void debug_line_test_plugin(ren::Application &app) {
  using namespace ren;

  app.world.entity("ren::menubar::Images");


  struct ImageViewerState {
    VkDescriptorSet imguiID = VK_NULL_HANDLE;
    ren::Image *selectedImage = nullptr;
    float thumbnailSize = 64.0f;
    ren::Sampler sampler{};
  };
  app.world.emplace<ImageViewerState>();


  app.world.system<ImageViewerState>("ren::core::TextureViewer").each([](ImageViewerState &state) {

    auto images = ren::Image::allImages();
    ImGui::Begin("Images");

    // Left panel - texture list with scrollbar
    ImGui::BeginChild("ImageList", ImVec2(200, 0), true);
    for (auto *image : images) {
      if (image->isFramebuffer()) continue;
      bool isSelected = (state.selectedImage == image);
      ImGui::PushID(image);
      if (ImGui::Selectable(image->getName().c_str(), isSelected)) {

        if (state.imguiID != VK_NULL_HANDLE) {
          ren::getVulkan().waitForIdle(); // HACK
          ImGui_ImplVulkan_RemoveTexture(state.imguiID);
          state.imguiID = VK_NULL_HANDLE;
        }
        state.selectedImage = image;

        state.imguiID = ImGui_ImplVulkan_AddTexture(state.sampler.getHandle(), image->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
      }
      ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right panel - image inspector
    ImGui::BeginChild("ImageInspector", ImVec2(0, 0), true);

    constexpr float maxImageSize = 720.0f;

    for (auto *image : images) {
      bool isSelected = (state.selectedImage == image);
      if (!isSelected) continue;

      // get the width of the ui region
      float width = ImGui::GetContentRegionAvail().x;
      if (width > maxImageSize) width = maxImageSize;

      // compute aspect ratio
      float aspectRatio = image->getWidth() / static_cast<float>(image->getHeight());
      float height = width / aspectRatio;

      // maximize the height to 720, and if it exceeds, scale down the width
      if (height > maxImageSize) {
        height = maxImageSize;
        width = height * aspectRatio;
      }

      if (state.imguiID != VK_NULL_HANDLE) {
        // Render image
        ImGui::Image(reinterpret_cast<void*>(state.imguiID), ImVec2(width, height));
      }

      ImGui::Separator();

      ImGui::Text("Name: %s", image->getName().c_str());
      ImGui::Text("Size: %dx%d", image->getWidth(), image->getHeight());

      const VkImageCreateInfo &info = image->createInfo();


      ImGui::Text("Format: %d", info.format);
      ImGui::Text("Samples: %d", info.samples);
      ImGui::Text("Tiling: %d", info.tiling);
      ImGui::Text("Usage: %d", info.usage);
      ImGui::Text("Sharing Mode: %d", info.sharingMode);
      ImGui::Text("Initial Layout: %d", info.initialLayout);

      break;
    }
    ImGui::EndChild();


    ImGui::End();
  });

  app.world
      .system<const ren::comp::Mesh, ren::comp::Transform>("ren::core::DebugLineDrawSystem")
      .each([](Entity e, const ren::comp::Mesh &m, ren::comp::Transform &t) {

        return;
        glm::mat4 worldTransform = getWorldTransform(e);
        auto mesh = m.mesh;
        auto bb = mesh->getAABB();

        ren::DebugScribe scribe(worldTransform);

        // clear the scale portion of the world transform
        // worldTransform[0][0] = 1.0f;
        // worldTransform[1][1] = 1.0f;
        // worldTransform[2][2] = 1.0f;

        // ren::emit<MyEvent>({e, 42});

        auto vbuf = mesh->getVertexBuffer();
        auto vertices = vbuf->map();
        for (int i = 0; i < mesh->getVertexCount(); i++) {
          auto &vertex = vertices[i];
          scribe.line(vertex.pos, vertex.pos + (vertex.normal * 0.2f), t.rotation * glm::vec4(vertex.normal, 1.0), 4.0f);
          // scribe.line(vertex.pos, vertex.pos + (glm::vec3(vertex.bitangent) * 0.2f), {0, 1, 0}, 1);
          // scribe.line(vertex.pos, vertex.pos + (glm::vec3(vertex.tangent) * 0.2f), {0, 0, 1}, 1);
        }
        vbuf->unmap();

        // draw a debug line around the bounding box.
        // debugLineLocal(worldTransform, bb.min, {bb.min.x, bb.min.y, bb.max.z}, {1, 1, 1}, 2.0f);
        // debugLineLocal(worldTransform, bb.min, {bb.min.x, bb.max.y, bb.min.z}, {1, 1, 1}, 2.0f);
        // debugLineLocal(worldTransform, bb.min, {bb.max.x, bb.min.y, bb.min.z}, {1, 1, 1}, 2.0f);
        // debugLineLocal(worldTransform, bb.max, {bb.min.x, bb.max.y, bb.max.z}, {1, 1, 1}, 2.0f);
        // debugLineLocal(worldTransform, bb.max, {bb.max.x, bb.min.y, bb.max.z}, {1, 1, 1}, 2.0f);
        // debugLineLocal(worldTransform, bb.max, {bb.max.x, bb.max.y, bb.min.z}, {1, 1, 1}, 2.0f);
      });
}
REN_PLUGIN("Debug Line Test", debug_line_test_plugin);





void loadMeshIntoScene(const char *path, float scaleChange = 0.0f) {
  fmt::println("Loading {}...", path);
  auto mesh = ren::MeshScene::load(path);
  if (!mesh) {
    fmt::print("Failed to load mesh from {}\n", path);
    return;
  }

  auto entity = mesh->instantiate({});
  entity.add<DebugLineDraw>();
  if (scaleChange != 0.0f) {
    entity.get_mut<ren::comp::Transform>().scale = glm::vec3(scaleChange);
  }
}

int main(int argc, char *argv[]) {
  try {
    ren::Application app("ren", {1920, 1080});

    // auto e = ren::createEntity();
    // add a directional light component
    // e.add<ren::comp::DirectionalLight>();

    // loadMeshIntoScene("/Users/nick/dev/kajiya/assets/meshes/viziers_observation_deck/scene.gltf", 0.01);
    loadMeshIntoScene("/Users/nick/dev/kajiya/assets/meshes/flying_world_-_battle_of_the_trash_god/scene.gltf", 0.002f);

    // loadMeshIntoScene("/Users/nick/Desktop/sponza.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf");
    // loadMeshIntoScene("/Users/nick/Downloads/pkg_a_curtains/NewSponza_Curtains_glTF.gltf");
    // loadMeshIntoScene("/Users/nick/Downloads/pkg_b_ivy/NewSponza_IvyGrowth_glTF.gltf");
    // loadMeshIntoScene("/Users/nick/Downloads/pkg_c_trees/NewSponza_CypressTree_glTF.gltf");

    // loadMeshIntoScene("/Users/nick/Downloads/huge_icelandic_lava_cliff_sieoz_high.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/Rock.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/Walk in the Woods.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/Fantasy Inn.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/broken_wall_slunl_high.glb");
    // loadMeshIntoScene("/Users/nick/Downloads/broken_stump_rkswd_raw.glb");


    // auto cube = ren::MeshScene::load("assets/test/meshes/unit_cube.glb");
    // auto e = cube->instantiate({});
    // e.add<DebugLineDraw>();

    /*
    int scale = 50;
    auto root = app.sceneLayer->scene.createEntity("root");
    for (int x = 0; x < scale; x++) {
      for (int z = 0; z < scale; z++) {
        auto e = app.sceneLayer->scene.createEntity().child_of(root).add<CubeWave>();

        ren::comp::Transform transform;
        transform.translation.x = (x - scale / 2) * 2.0f;
        transform.translation.y = 0;
        transform.translation.z = (z - scale / 2) * 2.0f;
        transform.scale = glm::vec3(0.75f);
        e.set<ren::comp::Transform>(transform);
        cube->instantiate(e);
      }
    }
    */

    app.world.entity<ren::RenderDebug>();

    app.run();
  } catch (const std::exception &e) { fmt::print("Error: {}\n", e.what()); }

  return 0;
}
