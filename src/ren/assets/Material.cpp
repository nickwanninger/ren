#include <ren/assets/Material.h>
#include <ren/renderer/shader/ShaderCache.h>

#include <cstring>
#include <stdexcept>

#include <fmt/format.h>
#include <imgui/imgui.h>

namespace ren {

  Material::Material(ref<ShaderProgram> shader) {
    pso.program = shader;
    if (!pso.program || !pso.program->getReflection()) {
      throw std::invalid_argument("Material needs a shader with reflection");
    }
    const auto *schema = getSchema();
    if (!schema) {
      throw std::invalid_argument("Material needs a shader with a MaterialRoot schema");
    }

    if (schema->byteSize == 0) {
      throw std::invalid_argument("Material needs a nonempty MaterialRoot schema");
    }
    cpuMaterialData.resize(schema->byteSize, 0);
    this->setName(pso.program->getPath());
  }


  const ShaderReflection::MaterialSchema *Material::getSchema() const {
    if (!pso.program || !pso.program->getReflection()) {
      return nullptr;
    }
    const auto &schema = pso.program->getReflection()->getMaterialSchema();
    return schema.isSome() ? &schema.unwrap() : nullptr;
  }

  const ShaderReflection::MaterialField &Material::field(std::string_view name) const {
    const auto *schema = getSchema();
    if (!schema) {
      throw std::runtime_error("Material has no reflected schema");
    }
    for (const auto &candidate : schema->fields) {
      if (candidate.name == name) {
        return candidate;
      }
    }
    throw std::invalid_argument(fmt::format("Material field '{}' does not exist", name));
  }

  void Material::writeField(const ShaderReflection::MaterialField &target, ren::ByteSpan value) {
    if (target.byteSize != value.size || target.byteOffset > cpuMaterialData.size() || value.size > cpuMaterialData.size() - target.byteOffset) {
      throw std::invalid_argument(fmt::format("Material field '{}' has an invalid byte layout", target.name));
    }
    std::memcpy(cpuMaterialData.data() + target.byteOffset, value.data, value.size);
  }

  void Material::setFloat(std::string_view name, float value) {
    const auto &target = field(name);
    if (target.kind != ShaderReflection::ValueKind::Scalar || target.scalarKind != ShaderReflection::ScalarKind::Float32) {
      throw std::invalid_argument(fmt::format("Material field '{}' is not a float", name));
    }
    writeField(target, value);
  }

  void Material::setFloat2(std::string_view name, const glm::vec2 &value) {
    const auto &target = field(name);
    if (target.kind != ShaderReflection::ValueKind::Vector || target.scalarKind != ShaderReflection::ScalarKind::Float32 ||
        target.rowCount * target.columnCount != 2) {
      throw std::invalid_argument(fmt::format("Material field '{}' is not a float2", name));
    }
    writeField(target, value);
  }

  void Material::setFloat3(std::string_view name, const glm::vec3 &value) {
    const auto &target = field(name);
    if (target.kind != ShaderReflection::ValueKind::Vector || target.scalarKind != ShaderReflection::ScalarKind::Float32 ||
        target.rowCount * target.columnCount != 3) {
      throw std::invalid_argument(fmt::format("Material field '{}' is not a float3", name));
    }
    writeField(target, value);
  }

  void Material::setFloat4(std::string_view name, const glm::vec4 &color) {
    const auto &target = field(name);
    if (target.kind != ShaderReflection::ValueKind::Vector || target.scalarKind != ShaderReflection::ScalarKind::Float32 ||
        target.rowCount * target.columnCount != 4) {
      throw std::invalid_argument(fmt::format("Material field '{}' is not a float4", name));
    }
    writeField(target, color);
  }


  void Material::setTexture(std::string_view name, const ref<Texture> &texture) {
    const auto &target = field(name);
    auto handle = texture->handle();
    // TODO: validate that the target is a texture type.
    writeField(target, ren::ByteSpan(handle));
    boundTextures[name.data()] = texture;
  }

  static void inspectMaterialFieldValueRecursive(const ShaderReflection::MaterialField &field, ren::ByteSpan rootData) {
    // This is an 'editor' inspection view. We want to show the field's name,
    // and then allow it to be edited if it is something we know how to provide
    // an editor for. Vectors and floats are easy.

    bool continueDown = !field.fields.empty();

    // Simple scalars.

    u8 *fieldData = rootData.data + field.byteOffset;

    if (field.typeName == "float") {
      ImGui::DragFloat(field.name.c_str(), reinterpret_cast<float *>(fieldData), 0.01f);
    } else if (field.typeName == "vector<float,2>") {
      ImGui::DragFloat2(field.name.c_str(), reinterpret_cast<float *>(fieldData), 0.01f);
    } else if (field.typeName == "vector<float,3>") {
      ImGui::ColorEdit3(field.name.c_str(), reinterpret_cast<float *>(fieldData));
    } else if (field.typeName == "vector<float,4>") {
      ImGui::ColorEdit4(field.name.c_str(), reinterpret_cast<float *>(fieldData));
    } else if (field.typeName == "uint") {
      ImGui::DragInt(field.name.c_str(), reinterpret_cast<int *>(fieldData), 0.01f);
    } else if (field.typeName == "vector<uint,2>") {
      ImGui::DragInt2(field.name.c_str(), reinterpret_cast<int *>(fieldData), 0.01f);
    } else if (field.typeName == "vector<uint,3>") {
      ImGui::DragInt3(field.name.c_str(), reinterpret_cast<int *>(fieldData));
    } else if (field.typeName == "vector<uint,4>") {
      ImGui::DragInt4(field.name.c_str(), reinterpret_cast<int *>(fieldData));
    } else {
      // We don't know how to edit this type, so we just show the name and
      // allow the user to expand it to see its children.
      ImGui::Text("%s (%s)", field.name.c_str(), field.typeName.c_str());
    }

    // If the field has children, we need to recurse into them. Otherwise, we can display the value directly.
    if (continueDown) {
      if (ImGui::TreeNode(&field, "%s", field.name.c_str())) {
        for (const auto &child : field.fields) {
          inspectMaterialFieldValueRecursive(child, rootData);
        }
        ImGui::TreePop();
      }
    }
  }

  void Material::inspect(void) {
    ImGui::Text("Material '%s'", name.c_str());

    if (const auto *schema = getSchema()) {
      schema->inspect();

      // Because we have the schema, we can inspect the material's fields and their values.
      for (const auto &field : schema->fields) {
        inspectMaterialFieldValueRecursive(field, ren::ByteSpan(cpuMaterialData));
      }
    }
  }

}  // namespace ren
