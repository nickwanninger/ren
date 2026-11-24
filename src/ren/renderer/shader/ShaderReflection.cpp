#include "./ShaderReflection.h"

#include <limits>
#include <slang-com-ptr.h>

namespace ren {
  using Type = ShaderReflection::Type;
  using Node = ShaderReflection::Node;

  namespace {

    const char* valueKindName(ShaderReflection::ValueKind kind) {
      using ValueKind = ShaderReflection::ValueKind;
      switch (kind) {
        case ValueKind::Scalar: return "scalar";
        case ValueKind::Vector: return "vector";
        case ValueKind::Matrix: return "matrix";
        case ValueKind::Struct: return "struct";
        case ValueKind::Array: return "array";
        case ValueKind::Pointer: return "pointer";
        case ValueKind::Enum: return "enum";
        default: return "unknown";
      }
    }

    const char* scalarKindName(ShaderReflection::ScalarKind kind) {
      using ScalarKind = ShaderReflection::ScalarKind;
      switch (kind) {
        case ScalarKind::Bool: return "bool";
        case ScalarKind::Int8: return "int8";
        case ScalarKind::UInt8: return "uint8";
        case ScalarKind::Int16: return "int16";
        case ScalarKind::UInt16: return "uint16";
        case ScalarKind::Int32: return "int32";
        case ScalarKind::UInt32: return "uint32";
        case ScalarKind::Int64: return "int64";
        case ScalarKind::UInt64: return "uint64";
        case ScalarKind::Float16: return "float16";
        case ScalarKind::Float32: return "float32";
        case ScalarKind::Float64: return "float64";
        default: return "none";
      }
    }

  }  // namespace

  json ShaderReflection::MaterialAttribute::toJson() const {
    return {
        {"name", name},
        {"arguments", arguments},
    };
  }

  json ShaderReflection::MaterialField::toJson() const {
    json result = {
        {"name", name},
        {"typeName", typeName},
        {"kind", valueKindName(kind)},
        {"offset", byteOffset},
        {"size", byteSize},
        {"alignment", alignment},
    };
    if (scalarKind != ScalarKind::None) result["scalarType"] = scalarKindName(scalarKind);
    if (rowCount != 0) result["rows"] = rowCount;
    if (columnCount != 0) result["columns"] = columnCount;
    if (elementCount != 0) result["elementCount"] = elementCount;
    if (elementStride != 0) result["elementStride"] = elementStride;
    if (!attributes.empty()) {
      result["attributes"] = json::array();
      for (const auto& attribute : attributes) {
        result["attributes"].push_back(attribute.toJson());
      }
    }
    if (!fields.empty()) {
      result["fields"] = json::array();
      for (const auto& field : fields) result["fields"].push_back(field.toJson());
    }
    return result;
  }

  json ShaderReflection::MaterialSchema::toJson() const {
    json result = {
        {"pushConstant", {
            {"name", pushConstantName},
            {"size", pushConstantByteSize},
            {"drawPointerOffset", drawPointerOffset},
            {"objectPointerOffset", objectPointerOffset},
            {"materialPointerOffset", materialPointerOffset},
        }},
        {"typeName", typeName},
        {"size", byteSize},
        {"alignment", alignment},
        {"fields", json::array()},
    };
    for (const auto& field : fields) result["fields"].push_back(field.toJson());
    return result;
  }

  const char* Node::getTypeName() const {
    switch (type) {
      case Type::Root:
        return "Root";
      case Type::PushConstant:
        return "PushConstant";
      case Type::Struct:
        return "Struct";
      case Type::Array:
        return "Array";
      case Type::Scalar:
        return "Scalar";
      case Type::Pointer:
        return "Pointer";
    }
    return "Unknown";
  }


  json ShaderReflection::Location::toJson() const {
    json result = json::object();
    if (pushConstant) {
      result["pushConstant"] = true;
    }
    if (byteOffset) {
      result["byteOffset"] = *byteOffset;
    }
    if (byteSize) {
      result["byteSize"] = *byteSize;
    }
    return result;
  }


  json Node::toJson() const {
    json result = {{"name", name}, {"type", getTypeName()}, {"location", location.toJson()}};
    if (!members.empty()) {
      result["members"] = json::array();
      for (const auto* member : members) {
        result["members"].push_back(member->toJson());
      }
    }
    return result;
  }


  namespace {
    // Slang field offsets are relative to their enclosing value. Only uniform
    // bytes and the push constant marker matter for this reflection model.
    void adjustLocation(slang::VariableLayoutReflection* variable, ShaderReflection::Location& location) {
      if (variable == nullptr) {
        return;
      }
      for (u32 i = 0; i < variable->getCategoryCount(); ++i) {
        const auto category = variable->getCategoryByIndex(i);
        if (category == slang::ParameterCategory::PushConstantBuffer) {
          location.pushConstant = true;
        } else if (category == slang::ParameterCategory::Uniform) {
          const auto offset = variable->getOffset(category);
          if (offset >= SLANG_UNKNOWN_SIZE || offset > std::numeric_limits<u32>::max()) {
            throw std::runtime_error("Push constant field has an unresolved byte offset");
          }
          location.byteOffset = (location.byteOffset ? *location.byteOffset : 0) + static_cast<u32>(offset);
        }
      }
    }
  }  // namespace


  namespace {

    u32 knownU32(size_t value) {
      return value < SLANG_UNKNOWN_SIZE && value <= std::numeric_limits<u32>::max()
          ? static_cast<u32>(value)
          : 0;
    }

    u32 knownAlignment(slang::TypeLayoutReflection* layout) {
      const auto alignment = layout->getAlignment(slang::ParameterCategory::Uniform);
      return alignment > 0 ? static_cast<u32>(alignment) : 0;
    }

    std::string reflectedTypeName(slang::TypeReflection* type) {
      if (type == nullptr) return {};

      Slang::ComPtr<slang::IBlob> name;
      if (SLANG_SUCCEEDED(type->getFullName(name.writeRef())) && name != nullptr) {
        return std::string(
            static_cast<const char*>(name->getBufferPointer()),
            name->getBufferSize());
      }
      return type->getName() != nullptr ? type->getName() : "";
    }

    ShaderReflection::ValueKind materialValueKind(slang::TypeReflection::Kind kind) {
      using Kind = slang::TypeReflection::Kind;
      using ValueKind = ShaderReflection::ValueKind;
      switch (kind) {
        case Kind::Scalar: return ValueKind::Scalar;
        case Kind::Vector: return ValueKind::Vector;
        case Kind::Matrix: return ValueKind::Matrix;
        case Kind::Struct: return ValueKind::Struct;
        case Kind::Array: return ValueKind::Array;
        case Kind::Pointer: return ValueKind::Pointer;
        case Kind::Enum: return ValueKind::Enum;
        default: return ValueKind::Unknown;
      }
    }

    ShaderReflection::ScalarKind materialScalarKind(slang::TypeReflection::ScalarType type) {
      using ScalarKind = ShaderReflection::ScalarKind;
      using ScalarType = slang::TypeReflection::ScalarType;
      switch (type) {
        case ScalarType::Bool: return ScalarKind::Bool;
        case ScalarType::Int8: return ScalarKind::Int8;
        case ScalarType::UInt8: return ScalarKind::UInt8;
        case ScalarType::Int16: return ScalarKind::Int16;
        case ScalarType::UInt16: return ScalarKind::UInt16;
        case ScalarType::Int32: return ScalarKind::Int32;
        case ScalarType::UInt32: return ScalarKind::UInt32;
        case ScalarType::Int64: return ScalarKind::Int64;
        case ScalarType::UInt64: return ScalarKind::UInt64;
        case ScalarType::Float16: return ScalarKind::Float16;
        case ScalarType::Float32: return ScalarKind::Float32;
        case ScalarType::Float64: return ScalarKind::Float64;
        default: return ScalarKind::None;
      }
    }

    slang::VariableLayoutReflection* findField(
        slang::TypeLayoutReflection* layout,
        std::string_view name) {
      if (layout == nullptr) return nullptr;
      for (u32 i = 0; i < layout->getFieldCount(); ++i) {
        auto* field = layout->getFieldByIndex(i);
        if (field != nullptr && field->getName() != nullptr && field->getName() == name) {
          return field;
        }
      }
      return nullptr;
    }

    void appendNestedMaterialFields(
        ShaderReflection::MaterialField& result,
        slang::TypeLayoutReflection* layout);

    json reflectAttributeArgument(slang::Attribute* attribute, u32 index) {
      int integerValue = 0;
      if (SLANG_SUCCEEDED(attribute->getArgumentValueInt(index, &integerValue))) {
        return integerValue;
      }

      float floatValue = 0.0f;
      if (SLANG_SUCCEEDED(attribute->getArgumentValueFloat(index, &floatValue))) {
        return floatValue;
      }

      size_t stringSize = 0;
      if (const char* stringValue = attribute->getArgumentValueString(index, &stringSize)) {
        return std::string(stringValue, stringSize);
      }

      throw std::runtime_error(fmt::format(
          "Material attribute '{}' has unsupported argument {} of type '{}'",
          attribute->getName() != nullptr ? attribute->getName() : "<unnamed>",
          index,
          reflectedTypeName(attribute->getArgumentType(index))));
    }

    ShaderReflection::MaterialAttribute reflectMaterialAttribute(
        slang::Attribute* attribute) {
      ShaderReflection::MaterialAttribute result;
      result.name = attribute->getName() != nullptr ? attribute->getName() : "";
      for (u32 i = 0; i < attribute->getArgumentCount(); ++i) {
        result.arguments.push_back(reflectAttributeArgument(attribute, i));
      }
      return result;
    }

    ShaderReflection::MaterialField reflectMaterialField(
        slang::VariableLayoutReflection* variable,
        slang::TypeLayoutReflection* layout) {
      ShaderReflection::MaterialField result;
      if (variable != nullptr && variable->getName() != nullptr) result.name = variable->getName();
      if (layout == nullptr) return result;

      result.typeName = reflectedTypeName(layout->getType());
      result.kind = materialValueKind(layout->getKind());
      result.scalarKind = materialScalarKind(layout->getScalarType());
      result.byteOffset = variable != nullptr
          ? knownU32(variable->getOffset(slang::ParameterCategory::Uniform))
          : 0;
      result.byteSize = knownU32(layout->getSize(slang::ParameterCategory::Uniform));
      result.alignment = knownAlignment(layout);

      if (result.kind == ShaderReflection::ValueKind::Vector ||
          result.kind == ShaderReflection::ValueKind::Matrix) {
        result.rowCount = layout->getRowCount();
        result.columnCount = layout->getColumnCount();
      }

      if (result.kind == ShaderReflection::ValueKind::Array) {
        result.elementCount = knownU32(layout->getElementCount());
        result.elementStride = knownU32(
            layout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM));
      }

      if (variable != nullptr && variable->getVariable() != nullptr) {
        auto* reflectedVariable = variable->getVariable();
        for (u32 i = 0; i < reflectedVariable->getUserAttributeCount(); ++i) {
          auto* attribute = reflectedVariable->getUserAttributeByIndex(i);
          if (attribute != nullptr && attribute->getName() != nullptr) {
            result.attributes.push_back(reflectMaterialAttribute(attribute));
          }
        }
      }

      appendNestedMaterialFields(result, layout);
      return result;
    }

    void appendNestedMaterialFields(
        ShaderReflection::MaterialField& result,
        slang::TypeLayoutReflection* layout) {
      auto* nestedLayout = layout;
      if (layout->getKind() == slang::TypeReflection::Kind::Array) {
        nestedLayout = layout->getElementTypeLayout();
      }
      if (nestedLayout == nullptr ||
          nestedLayout->getKind() != slang::TypeReflection::Kind::Struct) {
        return;
      }

      for (u32 i = 0; i < nestedLayout->getFieldCount(); ++i) {
        auto* field = nestedLayout->getFieldByIndex(i);
        if (field != nullptr) {
          result.fields.push_back(reflectMaterialField(field, field->getTypeLayout()));
        }
      }
    }

    bool hasTypeAttribute(slang::TypeReflection* type, std::string_view name) {
      if (type == nullptr) return false;
      for (u32 i = 0; i < type->getUserAttributeCount(); ++i) {
        auto* attribute = type->getUserAttributeByIndex(i);
        if (attribute != nullptr && attribute->getName() != nullptr &&
            attribute->getName() == name) {
          return true;
        }
      }
      return false;
    }

    Option<ShaderReflection::MaterialSchema> reflectMaterialPushConstant(
        slang::VariableLayoutReflection* parameter) {
      if (parameter == nullptr || parameter->getTypeLayout() == nullptr) return None;

      ShaderReflection::Location drawLocation;
      adjustLocation(parameter, drawLocation);
      auto* drawLayout = parameter->getTypeLayout();
      if (auto* container = drawLayout->getContainerVarLayout()) {
        adjustLocation(container, drawLocation);
      }
      if (!drawLocation.pushConstant ||
          drawLayout->getKind() != slang::TypeReflection::Kind::ConstantBuffer) {
        return None;
      }

      auto* drawParamsLayout = drawLayout->getElementTypeLayout();
      if (drawParamsLayout == nullptr ||
          !hasTypeAttribute(drawParamsLayout->getType(), "MaterialDrawParams")) {
        return None;
      }

      // The batched root holds three array base pointers. A draw selects its
      // object and material through draws[SV_DrawIndex], which is what
      // lets one push constant serve a whole indirect batch.
      auto* draws = findField(drawParamsLayout, "draws");
      auto* objects = findField(drawParamsLayout, "objects");
      auto* materials = findField(drawParamsLayout, "materials");
      auto isPointerField = [](slang::VariableLayoutReflection* field) {
        auto* layout = field != nullptr ? field->getTypeLayout() : nullptr;
        return layout != nullptr && layout->getKind() == slang::TypeReflection::Kind::Pointer;
      };
      if (!isPointerField(draws) || !isPointerField(objects) || !isPointerField(materials)) {
        throw std::runtime_error(
            "A MaterialDrawParams push constant must contain pointer fields named 'draws', 'objects' and 'materials'");
      }

      auto* materialPointerLayout = materials->getTypeLayout();

      auto* materialLayout = materialPointerLayout->getElementTypeLayout();
      if (materialLayout == nullptr ||
          materialLayout->getKind() != slang::TypeReflection::Kind::Struct) {
        return None;
      }

      ShaderReflection::MaterialSchema schema;
      schema.pushConstantName = parameter->getName() != nullptr ? parameter->getName() : "";
      schema.pushConstantByteSize = knownU32(
          drawParamsLayout->getSize(slang::ParameterCategory::Uniform));
      schema.drawPointerOffset = knownU32(
          draws->getOffset(slang::ParameterCategory::Uniform));
      schema.objectPointerOffset = knownU32(
          objects->getOffset(slang::ParameterCategory::Uniform));
      schema.materialPointerOffset = knownU32(
          materials->getOffset(slang::ParameterCategory::Uniform));
      schema.typeName = reflectedTypeName(materialLayout->getType());
      schema.byteSize = knownU32(
          materialLayout->getSize(slang::ParameterCategory::Uniform));
      schema.alignment = knownAlignment(materialLayout);
      for (u32 i = 0; i < materialLayout->getFieldCount(); ++i) {
        auto* field = materialLayout->getFieldByIndex(i);
        if (field != nullptr) {
          schema.fields.push_back(reflectMaterialField(field, field->getTypeLayout()));
        }
      }
      return schema;
    }

    Option<ShaderReflection::MaterialSchema> reflectMaterialSchema(
        slang::ProgramLayout* programLayout) {
      auto* globals = programLayout->getGlobalParamsVarLayout();
      auto* globalsLayout = globals != nullptr ? globals->getTypeLayout() : nullptr;
      if (globalsLayout == nullptr) return None;

      Option<ShaderReflection::MaterialSchema> result = None;
      for (u32 i = 0; i < globalsLayout->getFieldCount(); ++i) {
        auto schema = reflectMaterialPushConstant(globalsLayout->getFieldByIndex(i));
        if (schema.isNone()) continue;
        if (result.isSome()) {
          throw std::runtime_error(
              "A shader program cannot declare multiple MaterialDrawParams push constants");
        }
        result = std::move(schema);
      }
      return result;
    }

  }  // namespace


  Node* ShaderReflection::newNode(Type type, const char* name, const Location& location) {
    auto node = ren::makeBox<Node>();
    node->type = type;
    node->name = name;
    node->location = location;
    auto* result = node.get();
    allNodes.push_back(std::move(node));
    return result;
  }


  Node* ShaderReflection::extractValue(slang::VariableLayoutReflection* variable, slang::TypeLayoutReflection* layout, Location location) {
    adjustLocation(variable, location);
    const auto size = layout->getSize(slang::ParameterCategory::Uniform);
    if (size >= SLANG_UNKNOWN_SIZE || size > std::numeric_limits<u32>::max()) {
      throw std::runtime_error("Push constant field has an unresolved byte size");
    }
    location.byteSize = static_cast<u32>(size);
    const char* name = variable != nullptr && variable->getName() != nullptr ? variable->getName() : "";
    using Kind = slang::TypeReflection::Kind;

    switch (layout->getKind()) {
      case Kind::Struct: {
        auto* node = newNode(Type::Struct, name, location);
        for (u32 i = 0; i < layout->getFieldCount(); ++i) {
          auto* field = layout->getFieldByIndex(i);
          node->members.push_back(extractValue(field, field->getTypeLayout(), location));
        }
        return node;
      }
      case Kind::Array: {
        const auto count = layout->getElementCount();
        const auto stride = layout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
        if (count >= SLANG_UNKNOWN_SIZE || stride >= SLANG_UNKNOWN_SIZE) {
          throw std::runtime_error("Push constant array has an unresolved layout: " + std::string(name));
        }
        auto* node = newNode(Type::Array, name, location);
        for (size_t i = 0; i < count; ++i) {
          auto elementLocation = location;
          elementLocation.byteOffset = *location.byteOffset + static_cast<u32>(i * stride);
          auto* element = extractValue(layout->getElementVarLayout(), layout->getElementTypeLayout(), elementLocation);
          element->name = std::to_string(i);
          node->members.push_back(element);
        }
        return node;
      }
      case Kind::Pointer:
        return newNode(Type::Pointer, name, location);
      case Kind::Scalar:
      case Kind::Vector:
      case Kind::Matrix:
      case Kind::Enum:
        return newNode(Type::Scalar, name, location);
      default:
        throw std::runtime_error("Unsupported push constant field: " + std::string(name));
    }
  }


  void ShaderReflection::parseFromSlang(slang::ProgramLayout* programLayout) {
    if (programLayout == nullptr) {
      throw std::invalid_argument("Shader reflection needs a Slang program layout");
    }
    root = nullptr;
    allNodes.clear();
    materialSchema = reflectMaterialSchema(programLayout);
    root = newNode(Type::Root, "Root", {});

    auto* globals = programLayout->getGlobalParamsVarLayout();
    auto* globalsLayout = globals != nullptr ? globals->getTypeLayout() : nullptr;
    if (globalsLayout == nullptr) {
      return;
    }
    // Bare global uniforms can wrap the global scope in an implicit buffer.
    if (globalsLayout->getKind() == slang::TypeReflection::Kind::ConstantBuffer) {
      globalsLayout = globalsLayout->getElementTypeLayout();
    }
    for (u32 i = 0; i < globalsLayout->getFieldCount(); ++i) {
      auto* variable = globalsLayout->getFieldByIndex(i);
      auto* layout = variable->getTypeLayout();
      if (layout->getKind() != slang::TypeReflection::Kind::ConstantBuffer) {
        continue;
      }
      Location location;
      location.byteOffset = 0;
      adjustLocation(variable, location);
      adjustLocation(layout->getContainerVarLayout(), location);
      if (!location.pushConstant) {
        continue;
      }

      // Pair the element variable with its own layout to keep field offsets relative.
      auto* element = layout->getElementVarLayout();
      auto* elementLayout = element != nullptr ? element->getTypeLayout() : layout->getElementTypeLayout();
      auto* node = extractValue(element, elementLayout, location);
      node->name = variable->getName() != nullptr ? variable->getName() : "";
      node->type = Type::PushConstant;
      root->members.push_back(node);
    }
  }


  json ShaderReflection::toJson() const {
    json result = json::object();
    if (root != nullptr) {
      result["root"] = root->toJson();
    }
    if (materialSchema.isSome()) {
      result["material"] = materialSchema.unwrap().toJson();
    }
    return result;
  }
}  // namespace ren
