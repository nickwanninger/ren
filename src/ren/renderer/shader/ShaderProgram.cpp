#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/shader/ShaderReflection.h>
#include <ren/renderer/Swapchain.h>
#include <unistd.h>
#include <ren/renderer/Renderer.h>
#include <ren/renderer/FrameGlobals.h>
#include <ren/assets/AssetManager.h>
#include <fmt/format.h>
#include <ren/misc/DeprecationLogger.h>
#include <imgui/imgui.h>
#include <ren/core/ui/EditorUI.h>
#include <ren/core/Bundle.h>
#include <fstream>


namespace ren {
  namespace {
    bool isGlobalABIBinding(u32 set, u32 binding, VkDescriptorType type) {
      return
          (set == ShaderABI::frameSet &&
           binding == ShaderABI::frameBinding &&
           type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) ||
          (set == ShaderABI::sampledImageSet &&
           binding == ShaderABI::sampledImageBinding &&
           type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) ||
          (set == ShaderABI::samplerSet &&
           binding == ShaderABI::samplerBinding &&
           type == VK_DESCRIPTOR_TYPE_SAMPLER);
    }
  }  // namespace

  ShaderProgram::ShaderProgram(CompiledProgram&& compiled)
      : shaderPath(std::move(compiled.path))
      , reflection(std::move(compiled.reflection)) {
    createModules(compiled.entryPoints);

    for (auto& r : this->reflection->bindings) {
      ShaderBinding b = {
          .name = r.path,
          .set = r.set,
          .binding = r.index,
          .count = r.count,
          .type = r.type.toVkDescriptorType(),
          .stages = VK_SHADER_STAGE_ALL,  // TODO!!!
      };
      this->bindings.push_back(b);
    }

    validateAgainstABI();
    bakeLayouts();
  }

  ShaderProgram::~ShaderProgram() {
    auto& vulkan = getVulkan();

    // Delete the pipeline layout.
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(vulkan.device, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }

    // Descriptor set layouts are owned by Renderer.
  }


  void ShaderProgram::createModules(const std::vector<CompiledEntryPoint>& entryPoints) {
    shaders.reserve(entryPoints.size());
    for (const auto& ep : entryPoints) {
      shaders.push_back(make<ShaderModule>(ep.name, ep.spirv, ep.stage));
    }
  }


  void ShaderProgram::validateAgainstABI(void) const {
    for (const auto& binding : bindings) {
      if (!isGlobalABIBinding(binding.set, binding.binding, binding.type)) {
        throw std::runtime_error(fmt::format(
            "Shader '{}' declares descriptor set {} binding {} outside REN's global ABI; "
            "pass per-shader data through push constants, descriptor handles, or buffer addresses",
            shaderPath, binding.set, binding.binding));
      }
    }

    // Slang reflection intentionally omits its synthesized descriptor heaps.
    // Validate the emitted modules too, so the actual Vulkan ABI is enforced.
    ShaderReflection physicalReflection;
    for (const auto& shader : shaders) {
      physicalReflection.parseFromSpirv(
          reinterpret_cast<const u8*>(shader->getCode().data()),
          shader->getCode().size() * sizeof(u32));
    }
    for (const auto& binding : physicalReflection.bindings) {
      if (!isGlobalABIBinding(
              binding.set, binding.index,
              binding.type.toVkDescriptorType())) {
        throw std::runtime_error(fmt::format(
            "Shader '{}' emitted descriptor set {} binding {} outside REN's global ABI",
            shaderPath, binding.set, binding.index));
      }
    }
  }


  void ShaderProgram::bakeLayouts() {
    auto& vulkan = getVulkan();
    auto& renderer = Renderer::get();
    setLayouts = {
        renderer.getFrameGlobalsAllocator().getLayout(),
        renderer.getImageDescriptorHeap().getLayout(),
        renderer.getSamplerDescriptorHeap().getLayout(),
    };
    pushConstantRanges = {{
        .stageFlags = VK_SHADER_STAGE_ALL,
        .offset = 0,
        .size = ShaderABI::pushConstantBytes,
    }};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<u32>(setLayouts.size()),
        .pSetLayouts = setLayouts.data(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = pushConstantRanges.data(),
    };
    VK_CHECK(vkCreatePipelineLayout(
        vulkan.device, &pipelineLayoutInfo, nullptr, &pipelineLayout));
  }
  const ShaderBinding* ShaderProgram::getBinding(const std::string_view& name) const {
    // TODO: as we grow, we need a faster lookup mechanism!
    for (const auto& binding : bindings) {
      if (binding.name == name) {
        return &binding;
      }
    }
    return nullptr;  // Not found
  }

  const ShaderBinding* ShaderProgram::getBinding(u32 set, u32 binding) const {
    for (const auto& b : bindings) {
      if (b.set == set && b.binding == binding) {
        return &b;
      }
    }
    return nullptr;
  }



  void ShaderProgram::inspect(void) {
    auto colFlags = ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch;
    // if (eui::ButtonGreen("Serialize to Disk", ICON_SAVE)) {
    //   this->temporarySerialize("out/shaders");
    // }
    ImGui::Text("Shader Modules:");
    static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
    if (ImGui::BeginTable("##ShaderProgramModules", 5, flags)) {
      ImGui::TableSetupColumn("Name", colFlags);
      ImGui::TableSetupColumn("Type", colFlags);
      ImGui::TableSetupColumn("SPIR-V Size", colFlags);
      ImGui::TableSetupColumn("Handle", colFlags);
      ImGui::TableSetupColumn("##", colFlags);
      ImGui::TableHeadersRow();

      for (auto& module : shaders) {
        ImGui::PushID(&module);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s", module->getFilename().c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", module->getStage());

        ImGui::TableNextColumn();
        ImGui::Text("%zu bytes", module->getCode().size() * sizeof(u32));


        ImGui::TableNextColumn();
        ImGui::Text("%p", (void*)module->getHandle());

        ImGui::TableNextColumn();
        if (ImGui::Button(fmt::format("Dump##{}", module->getFilename()).c_str())) {
          // Dump SPIR-V to file
          auto dumpPath = fmt::format("{}.spv", module->getFilename());
          std::ofstream ofs(dumpPath, std::ios::binary);
          ofs.write(reinterpret_cast<const char*>(module->getCode().data()), module->getCode().size() * sizeof(u32));
          ofs.close();
          ren::println("Dumped SPIR-V to {}", dumpPath);

          // if `system` is defined, call spirv-dis on it
          system(fmt::format("spirv-dis {}", dumpPath).c_str());
          system(fmt::format("spirv-reflect {}", dumpPath).c_str());
          unlink(dumpPath.c_str());
          ren::ShaderReflection refl;
          refl.parseFromSpirv(reinterpret_cast<const u8*>(module->getCode().data()), module->getCode().size() * sizeof(u32));
          // ren::println("Reflection:\n{}", refl.getRoot()->toJson().dump(2));

          for (const auto& b : refl.bindings) {
            ren::println("Binding: set {} binding {} name {}", b.set, b.index, b.path);
          }
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }


    if (reflection) {
      ImGui::SeparatorText("Shader Reflection");
      reflection->inspect();
    }


    if (ImGui::BeginTable("##ShaderProgramBindings", 5, flags)) {
      ImGui::TableSetupColumn("Set", colFlags);
      ImGui::TableSetupColumn("Binding", colFlags);
      ImGui::TableSetupColumn("Type", colFlags);
      ImGui::TableSetupColumn("Count", colFlags);
      ImGui::TableSetupColumn("Stages", colFlags);
      ImGui::TableHeadersRow();

      for (const auto& binding : bindings) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.set);
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.binding);
        ImGui::TableNextColumn();
        ImGui::Text("%s", ren::VulkanInstance::stringifyEnum(binding.type));
        ImGui::TableNextColumn();
        ImGui::Text("%u", binding.count);
        ImGui::TableNextColumn();
        ImGui::Text("0x%X", binding.stages);
      }
      ImGui::EndTable();
    }


    ImGui::Separator();
    ImGui::Text("Descriptor Set Layouts:");
    if (ImGui::BeginTable("##ShaderProgramLayouts", 2, flags)) {
      ImGui::TableSetupColumn("Set", colFlags);
      ImGui::TableSetupColumn("Handle", colFlags);
      ImGui::TableHeadersRow();

      for (size_t i = 0; i < setLayouts.size(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%zu", i);
        ImGui::TableNextColumn();
        ImGui::Text("%p", (void*)setLayouts[i]);
      }
      ImGui::EndTable();
    }


    ImGui::Separator();
    ImGui::Text("Push Constant Ranges:");
    if (ImGui::BeginTable("##PushConstantRanges", 3, flags)) {
      ImGui::TableSetupColumn("Offset", colFlags);
      ImGui::TableSetupColumn("Size", colFlags);
      ImGui::TableSetupColumn("Stages", colFlags);
      ImGui::TableHeadersRow();

      for (const auto& range : pushConstantRanges) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%u", range.offset);
        ImGui::TableNextColumn();
        ImGui::Text("%u", range.size);
        ImGui::TableNextColumn();
        ImGui::Text("0x%X", range.stageFlags);
      }
      ImGui::EndTable();
    }


    ImGui::Separator();
  }


  void ShaderProgram::temporarySerialize(const std::string_view& outDir) {
    // Ensure output directory exists
    std::filesystem::create_directories(outDir);

    json j;
    j["reflection"] = reflection->toJson();


    ren::BundleBuilder bundle;

    // write the shader programs to disk as spirv.
    auto shadersJson = json::array();
    for (auto& shader : shaders) {
      json js;
      js["name"] = shader->getFilename();
      js["stage"] = shader->getStage();

      auto& code = shader->getCode();
      auto blobIndex = bundle.attachBlob(code);
      js["blob"] = blobIndex;

      shadersJson.push_back(js);
    }
    bundle.setKey("shaders", shadersJson);

    auto outPath = std::filesystem::path(outDir) / "shader_program.bundle";
    bundle.write(outPath.string());

    // // Serialize to JSON
    // auto json = this->toJson();

    // // Write to file
    // auto outPath = std::filesystem::path(outDir) / "shader_program.json";
    // std::ofstream ofs(outPath);
    // ofs << json.dump(2);
    // ofs.close();

    // ren::println("Serialized ShaderProgram to {}", outPath.string());
  }



}  // namespace ren
