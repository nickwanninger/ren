#include <ren/assets/Material.h>
#include <ren/renderer/shader/ShaderCache.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#include <fmt/format.h>
#include <glm/gtc/packing.hpp>
#include <imgui/imgui.h>

namespace ren {

  // TODO: Which code selects the pass? A material uses only the deferred pass until a second pass exists.
  static constexpr const char *kMaterialPass = "ren/pass/deferred";


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


  Material::Material(std::string_view surfaceShader)
      : Material(getShader(surfaceShader, kMaterialPass)) {
    this->surfaceShader = surfaceShader;
  }


  const ShaderReflection::MaterialSchema *Material::getSchema() const {
    if (!pso.program || !pso.program->getReflection()) {
      return nullptr;
    }
    const auto &schema = pso.program->getReflection()->getMaterialSchema();
    return schema.isSome() ? &schema.unwrap() : nullptr;
  }

  //=========================
  // .material files
  //=========================

  ref<Material> Material::fromContent(AssetID id, const json &j) {
    if (!j.contains("surface_shader") || !j["surface_shader"].is_string()) {
      throw std::runtime_error("Material missing surface_shader: " + id.to_string());
    }

    const auto surface = j["surface_shader"].get<std::string>();
    auto material = make<Material>(surface);
    material->id = id;
    if (j.contains("pso")) {
      from_json(j["pso"], material->pso);
    }

    if (!j.contains("fields")) {
      return material;
    }
    if (!j["fields"].is_object()) {
      throw std::runtime_error("Material fields must be an object: " + surface);
    }

    for (const auto &[name, entry] : j["fields"].items()) {
      if (!entry.is_object() || !entry.contains("value")) {
        throw std::runtime_error(fmt::format("Material field '{}' needs a value", name));
      }

      // A shader edit can remove or retype a field. The field then keeps its default.
      const auto *schema = material->getSchema();
      const auto target =
          std::find_if(schema->fields.begin(), schema->fields.end(), [&](const ShaderReflection::MaterialField &f) { return f.name == name; });
      if (target == schema->fields.end()) {
        ren::warnln("Material {}: shader {} has no field '{}'", id.to_string(), surface, name);
        continue;
      }
      if (!isStoredField(*target)) {
        ren::warnln("Material {}: field '{}' ({}) is not stored", id.to_string(), name, target->typeName);
        continue;
      }
      if (entry.contains("type") && entry["type"] != target->typeName) {
        ren::warnln("Material {}: field '{}' is now {}, not {}", id.to_string(), name, target->typeName, entry["type"].dump());
        continue;
      }

      material->setValue(name, entry["value"]);
    }
    return material;
  }


  json Material::serializeContent(void) const {
    if (surfaceShader.empty()) {
      throw std::runtime_error("Material has no surface shader: " + name);
    }

    json j;
    j["surface_shader"] = surfaceShader;

    // The surface shader replaces the PSO's program entry.
    j["pso"] = pso;
    j["pso"].erase("program");

    auto fields = json::object();
    for (const auto &target : getSchema()->fields) {
      if (isStoredField(target)) {
        fields[target.name] = {{"type", target.typeName}, {"value", getValue(target.name)}};
      }
    }
    j["fields"] = std::move(fields);
    return j;
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

  //=========================
  // Field values
  //=========================

  namespace {

    using Field = ShaderReflection::MaterialField;
    using ValueKind = ShaderReflection::ValueKind;
    using ScalarKind = ShaderReflection::ScalarKind;

    // The byte size of one scalar, or 0 if the JSON value format cannot store the kind.
    // Material buffers use scalar layout, so each kind has its natural size. Slang stores
    // a bool in a buffer as 32 bits.
    u32 scalarSize(ScalarKind kind) {
      switch (kind) {
        case ScalarKind::Int8:
        case ScalarKind::UInt8:
          return 1;
        case ScalarKind::Int16:
        case ScalarKind::UInt16:
        case ScalarKind::Float16:
          return 2;
        case ScalarKind::Bool:
        case ScalarKind::Int32:
        case ScalarKind::UInt32:
        case ScalarKind::Float32:
          return 4;
        case ScalarKind::Int64:
        case ScalarKind::UInt64:
        case ScalarKind::Float64:
          return 8;
        default:
          return 0;
      }
    }


    // The scalar count of a vector. Slang can report a vector as one row or one column.
    u32 vectorLength(const Field &field) { return field.rowCount * field.columnCount; }


    // The distance between matrix columns. The session uses column-major layout, so the
    // reflected size covers columnCount columns, each with its padding.
    u32 columnStride(const Field &field) { return field.byteSize / field.columnCount; }


    // Throws if the reflected size does not hold the scalars that the field's shape needs.
    void checkLayout(const Field &field) {
      const u32 size = scalarSize(field.scalarKind);
      bool valid = false;
      switch (field.kind) {
        case ValueKind::Scalar:
          valid = field.byteSize == size;
          break;
        case ValueKind::Vector:
          valid = field.byteSize == vectorLength(field) * size;
          break;
        case ValueKind::Matrix:
          valid = field.columnCount > 0 && field.byteSize % field.columnCount == 0 && columnStride(field) >= field.rowCount * size;
          break;
        default:
          break;
      }
      if (!valid) {
        throw std::invalid_argument(fmt::format("Material field '{}' ({}) has an invalid byte layout", field.name, field.typeName));
      }
    }


    // Writes an integer of type T if `value` is an integer in the range of T.
    template <typename T>
    bool encodeInteger(const json &value, u8 *out) {
      if (!value.is_number_integer()) {
        return false;
      }

      // nlohmann::json keeps a parsed integer that is not negative as unsigned, and an
      // integer from C++ as signed. Check the range in the form that the value has.
      bool fits = false;
      if (value.is_number_unsigned()) {
        fits = value.get<u64>() <= static_cast<u64>(std::numeric_limits<T>::max());
      } else {
        const auto signedValue = value.get<i64>();
        fits = signedValue >= 0 ? static_cast<u64>(signedValue) <= static_cast<u64>(std::numeric_limits<T>::max())
                                : signedValue >= static_cast<i64>(std::numeric_limits<T>::min());
      }
      if (!fits) {
        return false;
      }

      const auto scalar = value.get<T>();
      std::memcpy(out, &scalar, sizeof(T));
      return true;
    }


    template <typename T>
    bool encodeFloat(const json &value, u8 *out) {
      if (!value.is_number()) {
        return false;
      }
      const auto scalar = value.get<T>();
      std::memcpy(out, &scalar, sizeof(T));
      return true;
    }


    template <typename T>
    json decodeAs(const u8 *data) {
      T scalar;
      std::memcpy(&scalar, data, sizeof(T));
      return scalar;
    }


    void encodeScalar(const Field &field, const json &value, u8 *out) {
      bool written = false;
      switch (field.scalarKind) {
        case ScalarKind::Bool:
          if (value.is_boolean()) {
            const u32 scalar = value.get<bool>() ? 1 : 0;
            std::memcpy(out, &scalar, sizeof(scalar));
            written = true;
          }
          break;

        case ScalarKind::Int8:
          written = encodeInteger<i8>(value, out);
          break;
        case ScalarKind::UInt8:
          written = encodeInteger<u8>(value, out);
          break;
        case ScalarKind::Int16:
          written = encodeInteger<i16>(value, out);
          break;
        case ScalarKind::UInt16:
          written = encodeInteger<u16>(value, out);
          break;
        case ScalarKind::Int32:
          written = encodeInteger<i32>(value, out);
          break;
        case ScalarKind::UInt32:
          written = encodeInteger<u32>(value, out);
          break;
        case ScalarKind::Int64:
          written = encodeInteger<i64>(value, out);
          break;
        case ScalarKind::UInt64:
          written = encodeInteger<u64>(value, out);
          break;

        case ScalarKind::Float16:
          if (value.is_number()) {
            const u16 scalar = glm::packHalf1x16(value.get<float>());
            std::memcpy(out, &scalar, sizeof(scalar));
            written = true;
          }
          break;
        case ScalarKind::Float32:
          written = encodeFloat<float>(value, out);
          break;
        case ScalarKind::Float64:
          written = encodeFloat<double>(value, out);
          break;

        default:
          break;
      }

      if (!written) {
        throw std::invalid_argument(fmt::format("Material field '{}' ({}) cannot hold the value {}", field.name, field.typeName, value.dump()));
      }
    }


    json decodeScalar(const Field &field, const u8 *data) {
      switch (field.scalarKind) {
        case ScalarKind::Bool:
          return decodeAs<u32>(data) != 0;
        case ScalarKind::Int8:
          return decodeAs<i8>(data);
        case ScalarKind::UInt8:
          return decodeAs<u8>(data);
        case ScalarKind::Int16:
          return decodeAs<i16>(data);
        case ScalarKind::UInt16:
          return decodeAs<u16>(data);
        case ScalarKind::Int32:
          return decodeAs<i32>(data);
        case ScalarKind::UInt32:
          return decodeAs<u32>(data);
        case ScalarKind::Int64:
          return decodeAs<i64>(data);
        case ScalarKind::UInt64:
          return decodeAs<u64>(data);
        case ScalarKind::Float16: {
          u16 scalar;
          std::memcpy(&scalar, data, sizeof(scalar));
          return glm::unpackHalf1x16(scalar);
        }
        case ScalarKind::Float32:
          return decodeAs<float>(data);
        case ScalarKind::Float64:
          return decodeAs<double>(data);
        default:
          throw std::invalid_argument(fmt::format("Material field '{}' ({}) is not stored", field.name, field.typeName));
      }
    }


    // Encodes `count` scalars from a JSON array that must have exactly `count` elements.
    void encodeScalars(const Field &field, const json &value, u32 count, u8 *out) {
      if (!value.is_array() || value.size() != count) {
        throw std::invalid_argument(
            fmt::format("Material field '{}' ({}) needs an array of {} values, not {}", field.name, field.typeName, count, value.dump()));
      }
      const u32 size = scalarSize(field.scalarKind);
      for (u32 i = 0; i < count; i++) {
        encodeScalar(field, value[i], out + i * size);
      }
    }


    json decodeScalars(const Field &field, u32 count, const u8 *data) {
      const u32 size = scalarSize(field.scalarKind);
      auto result = json::array();
      for (u32 i = 0; i < count; i++) {
        result.push_back(decodeScalar(field, data + i * size));
      }
      return result;
    }

  }  // namespace


  bool Material::isStoredField(const ShaderReflection::MaterialField &field) {
    if (field.name.empty() || field.name.starts_with('_') || scalarSize(field.scalarKind) == 0) {
      return false;
    }
    return field.kind == ValueKind::Scalar || field.kind == ValueKind::Vector || (field.kind == ValueKind::Matrix && field.columnCount > 0);
  }


  void Material::encodeValue(const ShaderReflection::MaterialField &field, const json &value, u8 *out) {
    checkLayout(field);
    switch (field.kind) {
      case ValueKind::Scalar:
        encodeScalar(field, value, out);
        return;
      case ValueKind::Vector:
        encodeScalars(field, value, vectorLength(field), out);
        return;

      case ValueKind::Matrix:
        if (!value.is_array() || value.size() != field.columnCount) {
          throw std::invalid_argument(fmt::format("Material field '{}' ({}) needs an array of {} columns, not {}", field.name, field.typeName,
                                                  field.columnCount, value.dump()));
        }
        for (u32 column = 0; column < field.columnCount; column++) {
          encodeScalars(field, value[column], field.rowCount, out + column * columnStride(field));
        }
        return;

      default:
        throw std::invalid_argument(fmt::format("Material field '{}' ({}) is not stored", field.name, field.typeName));
    }
  }


  json Material::decodeValue(const ShaderReflection::MaterialField &field, const u8 *data) {
    checkLayout(field);
    switch (field.kind) {
      case ValueKind::Scalar:
        return decodeScalar(field, data);
      case ValueKind::Vector:
        return decodeScalars(field, vectorLength(field), data);

      case ValueKind::Matrix: {
        auto columns = json::array();
        for (u32 column = 0; column < field.columnCount; column++) {
          columns.push_back(decodeScalars(field, field.rowCount, data + column * columnStride(field)));
        }
        return columns;
      }

      default:
        throw std::invalid_argument(fmt::format("Material field '{}' ({}) is not stored", field.name, field.typeName));
    }
  }


  void Material::setValue(std::string_view name, const json &value) {
    const auto &target = field(name);
    if (!isStoredField(target)) {
      throw std::invalid_argument(fmt::format("Material field '{}' ({}) is not stored", name, target.typeName));
    }

    // Encode into a copy, so that a value that does not fit changes no bytes.
    std::vector<u8> bytes(target.byteSize, 0);
    encodeValue(target, value, bytes.data());
    writeField(target, ren::ByteSpan(bytes));
  }


  json Material::getValue(std::string_view name) const {
    const auto &target = field(name);
    if (!isStoredField(target)) {
      throw std::invalid_argument(fmt::format("Material field '{}' ({}) is not stored", name, target.typeName));
    }
    if (target.byteOffset > cpuMaterialData.size() || target.byteSize > cpuMaterialData.size() - target.byteOffset) {
      throw std::invalid_argument(fmt::format("Material field '{}' has an invalid byte layout", target.name));
    }
    return decodeValue(target, cpuMaterialData.data() + target.byteOffset);
  }


  void Material::setFloat(std::string_view name, float value) { setValue(name, value); }

  void Material::setFloat2(std::string_view name, const glm::vec2 &value) { setValue(name, json::array({value.x, value.y})); }

  void Material::setFloat3(std::string_view name, const glm::vec3 &value) { setValue(name, json::array({value.x, value.y, value.z})); }

  void Material::setFloat4(std::string_view name, const glm::vec4 &value) { setValue(name, json::array({value.x, value.y, value.z, value.w})); }


  void Material::setTexture(std::string_view name, const ref<Texture> &texture) {
    const auto &target = field(name);
    auto bindless = texture->bindless();
    // TODO: validate that the target is a texture type.
    writeField(target, ren::ByteSpan(bindless));
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
        if (field.name.starts_with('_')) {
          continue;
        }
        inspectMaterialFieldValueRecursive(field, ren::ByteSpan(cpuMaterialData));
      }
    }
  }

}  // namespace ren
