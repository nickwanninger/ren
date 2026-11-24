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
#include <spirv_reflect/spirv_reflect.h>


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
    // Slang omits synthesized descriptor heaps from its program layout. Inspect
    // emitted descriptors directly; validation needs no field or resource tree.
    for (const auto& shader : shaders) {
      SpvReflectShaderModule module{};
      const auto& code = shader->getCode();
      if (spvReflectCreateShaderModule(code.size() * sizeof(u32), code.data(), &module) != SPV_REFLECT_RESULT_SUCCESS) {
        throw std::runtime_error("Failed to inspect emitted shader: " + shaderPath);
      }
      // Destruction also applies when descriptor validation throws.
      struct ModuleGuard {
        SpvReflectShaderModule* module;
        ~ModuleGuard() { spvReflectDestroyShaderModule(module); }
      } guard{&module};

      for (u32 i = 0; i < module.descriptor_binding_count; ++i) {
        const auto& binding = module.descriptor_bindings[i];
        if (!isGlobalABIBinding(binding.set, binding.binding, static_cast<VkDescriptorType>(binding.descriptor_type))) {
          throw std::runtime_error(
              fmt::format("Shader '{}' emitted descriptor set {} binding {} outside REN's global ABI", shaderPath, binding.set, binding.binding));
        }
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
          // unlink(dumpPath.c_str());

        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }


    if (reflection) {
      ImGui::SeparatorText("Shader Reflection");
      reflection->inspect();
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


    ren::Bundle bundle;

    // write the shader programs to disk as spirv.
    auto shadersJson = json::array();
    for (auto& shader : shaders) {
      json js;
      js["name"] = shader->getFilename();
      js["stage"] = shader->getStage();

      auto& code = shader->getCode();
      auto blobIndex = bundle.attachBlob(shader->getFilename(), code);
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
