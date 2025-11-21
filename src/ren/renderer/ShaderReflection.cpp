#include "./ShaderReflection.h"
#include <spirv_reflect/spirv_reflect.h>
#include <unordered_map>
#include <string>

namespace ren {

// ============================================================================
// Node Class Implementations
// ============================================================================

// Type alias for convenience within this namespace
using BindingType = ShaderReflection::BindingType;
using INode = ShaderReflection::INode;

// Binding type enum values
constexpr auto UniformBuffer = ShaderReflection::UniformBuffer;
constexpr auto StorageBuffer = ShaderReflection::StorageBuffer;
constexpr auto Texture = ShaderReflection::Texture;
constexpr auto Sampler = ShaderReflection::Sampler;
constexpr auto Image = ShaderReflection::Image;
constexpr auto PushConstant = ShaderReflection::PushConstant;
constexpr auto Struct = ShaderReflection::Struct;
constexpr auto Array = ShaderReflection::Array;
constexpr auto Field = ShaderReflection::Field;
constexpr auto Unknown = ShaderReflection::Unknown;

// Base node class for common functionality
class BaseNode : public INode {
public:
  std::string m_name;
  std::vector<INode*> m_members;

public:
  BaseNode(std::string_view name) : m_name(name) {}
  virtual ~BaseNode() = default;

  std::string_view name() const override { return m_name; }

  INode* getMember(const char* name) const override {
    if (!name) return nullptr;
    for (auto member : m_members) {
      if (member && std::string_view(member->name()) == name) {
        return member;
      }
    }
    return nullptr;
  }

  INode* getMember(u32 index) const override {
    if (index < m_members.size()) {
      return m_members[index];
    }
    return nullptr;
  }
};

// Root node representing the entire shader's reflection
class RootNode : public BaseNode {
public:
  RootNode() : BaseNode("root") {}

  BindingType bindingType() const override { return Struct; }

  json toJson() const override {
    json j;
    j["name"] = std::string(name());
    j["type"] = "Struct";
    if (!m_members.empty()) {
      json members_json = json::array();
      for (auto member : m_members) {
        members_json.push_back(member->toJson());
      }
      j["members"] = members_json;
    }
    return j;
  }
};

// Uniform or Storage buffer node
class BufferNode : public BaseNode {
private:
  u32 m_set;
  u32 m_binding;
  u32 m_size;
  BindingType m_bufferType;

public:
  BufferNode(std::string_view name, u32 set, u32 binding, u32 size,
             BindingType type)
      : BaseNode(name), m_set(set), m_binding(binding), m_size(size),
        m_bufferType(type) {}

  BindingType bindingType() const override { return m_bufferType; }
  std::optional<u32> size() const override { return m_size; }
  std::optional<std::pair<u32, u32>> binding() const override {
    return std::make_pair(m_set, m_binding);
  }

  json toJson() const override {
    json j;
    j["name"] = std::string(name());
    j["type"] = (m_bufferType == UniformBuffer) ? "UniformBuffer"
                                                  : "StorageBuffer";
    j["binding"] = {{"set", m_set}, {"index", m_binding}};
    j["size"] = m_size;
    if (!m_members.empty()) {
      json members_json = json::array();
      for (auto member : m_members) {
        members_json.push_back(member->toJson());
      }
      j["members"] = members_json;
    }
    return j;
  }
};

// Texture, Sampler, or Image resource node
class ResourceNode : public BaseNode {
private:
  u32 m_set;
  u32 m_binding;
  BindingType m_resourceType;

public:
  ResourceNode(std::string_view name, u32 set, u32 binding, BindingType type)
      : BaseNode(name), m_set(set), m_binding(binding), m_resourceType(type) {}

  BindingType bindingType() const override { return m_resourceType; }
  std::optional<std::pair<u32, u32>> binding() const override {
    return std::make_pair(m_set, m_binding);
  }

  json toJson() const override {
    json j;
    j["name"] = std::string(name());
    const char* type_str = "Unknown";
    if (m_resourceType == Texture)
      type_str = "Texture";
    else if (m_resourceType == Sampler)
      type_str = "Sampler";
    else if (m_resourceType == Image)
      type_str = "Image";
    j["type"] = type_str;
    j["binding"] = {{"set", m_set}, {"index", m_binding}};
    return j;
  }
};

// Push constant block node
class PushConstantNode : public BaseNode {
private:
  u32 m_size;

public:
  PushConstantNode(std::string_view name, u32 size)
      : BaseNode(name), m_size(size) {}

  BindingType bindingType() const override { return PushConstant; }
  std::optional<u32> size() const override { return m_size; }

  json toJson() const override {
    json j;
    j["name"] = std::string(name());
    j["type"] = "PushConstant";
    j["size"] = m_size;
    if (!m_members.empty()) {
      json members_json = json::array();
      for (auto member : m_members) {
        members_json.push_back(member->toJson());
      }
      j["members"] = members_json;
    }
    return j;
  }
};

// Struct node (for nested structs in buffers/push constants)
class StructNode : public BaseNode {
private:
  std::optional<u32> m_size;

public:
  StructNode(std::string_view name, std::optional<u32> size = std::nullopt)
      : BaseNode(name), m_size(size) {}

  BindingType bindingType() const override { return Struct; }
  std::optional<u32> size() const override { return m_size; }

  json toJson() const override {
    json j;
    j["name"] = std::string(name());
    j["type"] = "Struct";
    if (m_size) {
      j["size"] = *m_size;
    }
    if (!m_members.empty()) {
      json members_json = json::array();
      for (auto member : m_members) {
        members_json.push_back(member->toJson());
      }
      j["members"] = members_json;
    }
    return j;
  }
};

// Array node representing arrays of resources or structs
class ArrayNode : public BaseNode {
private:
  u32 m_arraySize;
  std::optional<std::pair<u32, u32>> m_binding;
  BindingType m_elementBindingType;

public:
  ArrayNode(std::string_view name, u32 arraySize,
            BindingType elementType,
            std::optional<std::pair<u32, u32>> binding = std::nullopt)
      : BaseNode(name), m_arraySize(arraySize), m_binding(binding),
        m_elementBindingType(elementType) {}

  BindingType bindingType() const override { return Array; }
  std::optional<u32> size() const override {
    // Array size is the count, not byte size
    return m_arraySize;
  }
  std::optional<std::pair<u32, u32>> binding() const override {
    return m_binding;
  }

  json toJson() const override {
    json j;
    j["name"] = std::string(name());
    j["type"] = "Array";
    j["arraySize"] = m_arraySize;
    if (m_binding) {
      j["binding"] = {{"set", m_binding->first}, {"index", m_binding->second}};
    }
    return j;
  }
};

// Field node (member of a struct or buffer)
class FieldNode : public BaseNode {
private:
  u32 m_offset;
  u32 m_size;

public:
  FieldNode(std::string_view name, u32 offset, u32 size)
      : BaseNode(name), m_offset(offset), m_size(size) {}

  BindingType bindingType() const override { return Field; }
  std::optional<u32> size() const override { return m_size; }

  json toJson() const override {
    json j;
    j["name"] = std::string(name());
    j["type"] = "Field";
    j["offset"] = m_offset;
    j["size"] = m_size;
    return j;
  }
};

// ============================================================================
// Helper Functions for Parsing
// ============================================================================

static BindingType mapDescriptorType(SpvReflectDescriptorType type) {
  switch (type) {
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return UniformBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      return StorageBuffer;
    case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return Texture;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      return Texture;
    case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
      return Sampler;
    case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      return Image;
    default:
      return Unknown;
  }
}

// ============================================================================
// ShaderReflection::parseFromSpirv Implementation
// ============================================================================

void ShaderReflection::parseFromSpirv(const u8* spirvData,
                                      size_t spirvSize) {
  // Create and parse the SPIR-V module
  SpvReflectShaderModule module = {};
  SpvReflectResult result =
      spvReflectCreateShaderModule(spirvSize, spirvData, &module);

  if (result != SPV_REFLECT_RESULT_SUCCESS) {
    std::cerr << "Failed to parse SPIR-V: " << result << std::endl;
    return;
  }

  // Create root node
  auto root = makeBox<RootNode>();
  INode* root_ptr = root.get();
  allNodes.push_back(std::move(root));

  // Parse descriptor bindings
  uint32_t binding_count = 0;
  spvReflectEnumerateDescriptorBindings(&module, &binding_count, nullptr);

  if (binding_count > 0) {
    std::vector<SpvReflectDescriptorBinding*> bindings(binding_count);
    spvReflectEnumerateDescriptorBindings(&module, &binding_count,
                                          bindings.data());

    for (const auto* binding : bindings) {
      if (!binding || !binding->name) continue;

      BindingType type = mapDescriptorType(binding->descriptor_type);

      // Handle array bindings
      if (binding->array.dims_count > 0) {
        u32 array_size = binding->array.dims[0];
        auto array_node =
            makeBox<ArrayNode>(binding->name, array_size, type,
                               std::make_pair(binding->set, binding->binding));
        INode* array_ptr = array_node.get();
        allNodes.push_back(std::move(array_node));

        // Cast and add to root
        BaseNode* root_base =
            const_cast<BaseNode*>(dynamic_cast<const BaseNode*>(root_ptr));
        if (root_base) {
          root_base->m_members.push_back(array_ptr);
        }
      } else if (type == UniformBuffer || type == StorageBuffer) {
        // Create buffer node
        auto buffer_node = makeBox<BufferNode>(
            binding->name, binding->set, binding->binding, binding->block.size,
            type);
        INode* buffer_ptr = buffer_node.get();
        allNodes.push_back(std::move(buffer_node));

        // Parse buffer members
        if (binding->block.member_count > 0) {
          for (uint32_t i = 0; i < binding->block.member_count; ++i) {
            const auto* member = &binding->block.members[i];
            if (member->type_description &&
                member->type_description->op == SpvOpTypeStruct &&
                member->member_count > 0) {
              // Nested struct
              auto struct_node = makeBox<StructNode>(
                  member->name, std::optional<u32>(member->size));
              INode* struct_ptr = struct_node.get();
              allNodes.push_back(std::move(struct_node));

              // Cast and add to buffer
              BaseNode* buffer_base = const_cast<BaseNode*>(
                  dynamic_cast<const BaseNode*>(buffer_ptr));
              if (buffer_base) {
                buffer_base->m_members.push_back(struct_ptr);

                // Add struct members
                for (uint32_t j = 0; j < member->member_count; ++j) {
                  auto field = makeBox<FieldNode>(member->members[j].name,
                                                  member->members[j].offset,
                                                  member->members[j].size);
                  INode* field_ptr = field.get();
                  allNodes.push_back(std::move(field));
                  buffer_base->m_members.push_back(field_ptr);
                }
              }
            } else {
              // Regular field
              auto field = makeBox<FieldNode>(member->name, member->offset,
                                              member->size);
              INode* field_ptr = field.get();
              allNodes.push_back(std::move(field));

              // Cast and add to buffer
              BaseNode* buffer_base = const_cast<BaseNode*>(
                  dynamic_cast<const BaseNode*>(buffer_ptr));
              if (buffer_base) {
                buffer_base->m_members.push_back(field_ptr);
              }
            }
          }
        }

        // Cast and add to root
        BaseNode* root_base =
            const_cast<BaseNode*>(dynamic_cast<const BaseNode*>(root_ptr));
        if (root_base) {
          root_base->m_members.push_back(buffer_ptr);
        }
      } else {
        // Texture, Sampler, Image
        auto resource_node = makeBox<ResourceNode>(binding->name, binding->set,
                                                   binding->binding, type);
        INode* resource_ptr = resource_node.get();
        allNodes.push_back(std::move(resource_node));

        // Cast and add to root
        BaseNode* root_base =
            const_cast<BaseNode*>(dynamic_cast<const BaseNode*>(root_ptr));
        if (root_base) {
          root_base->m_members.push_back(resource_ptr);
        }
      }
    }
  }

  // Parse push constant blocks
  for (uint32_t i = 0; i < module.push_constant_block_count; ++i) {
    const auto* pc_block = &module.push_constant_blocks[i];
    if (!pc_block || !pc_block->name) continue;

    auto pc_node =
        makeBox<PushConstantNode>(pc_block->name, pc_block->size);
    INode* pc_ptr = pc_node.get();
    allNodes.push_back(std::move(pc_node));

    // Parse push constant members
    if (pc_block->member_count > 0) {
      for (uint32_t j = 0; j < pc_block->member_count; ++j) {
        const auto* member = &pc_block->members[j];
        if (member->name) {
          auto field =
              makeBox<FieldNode>(member->name, member->offset, member->size);
          INode* field_ptr = field.get();
          allNodes.push_back(std::move(field));

          // Cast and add to push constant
          BaseNode* pc_base =
              const_cast<BaseNode*>(dynamic_cast<const BaseNode*>(pc_ptr));
          if (pc_base) {
            pc_base->m_members.push_back(field_ptr);
          }
        }
      }
    }

    // Cast and add to root
    BaseNode* root_base =
        const_cast<BaseNode*>(dynamic_cast<const BaseNode*>(root_ptr));
    if (root_base) {
      root_base->m_members.push_back(pc_ptr);
    }
  }

  // Clean up spirv-reflect module
  spvReflectDestroyShaderModule(&module);

  // Set root node
  this->root = root_ptr;
}

}  // namespace ren