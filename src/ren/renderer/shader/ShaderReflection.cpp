#include "./ShaderReflection.h"
#include <unordered_map>
#include <string>
#include <set>

#include <slang-com-ptr.h>
#include <slang.h>
#include "./SlangPrinter.h"

namespace ren {




  // ============================================================================
  // Node Class Implementations
  // ============================================================================




  // Type alias for convenience within this namespace
  using Type = ShaderReflection::Type;
  using BindingType = ShaderReflection::BindingType;
  using Node = ShaderReflection::Node;

  const char* bindingTypeToString(Type type) {
    switch (type) {
#define TYPE(a, ...) \
  case Type::a:      \
    return #a;
#include "./ShaderReflectionTypes.def"
#undef TYPE
      case Type::Unknown:
        return "Unknown";
    }
  }

  // Make a child of this location
  ShaderReflection::Location ShaderReflection::Location::child(void) const {
    Location loc = *this;
    return loc;
  }

  json ShaderReflection::Location::toJson() const {
    json result;
    // result["depth"] = depth;
    if (pushConstant) {
      result["pushConstant"] = pushConstant;
    }
    if (bindingSet) {
      result["bindingSet"] = *bindingSet;
    }
    if (bindingIndex) {
      result["bindingIndex"] = *bindingIndex;
    }
    if (byteOffset) {
      result["byteOffset"] = *byteOffset;
    }
    if (byteSize) {
      result["byteSize"] = *byteSize;
    }
    if (arrayIndex) {
      result["arrayIndex"] = *arrayIndex;
    }
    if (varyingIn) {
      result["varyingIn"] = *varyingIn;
    }
    if (varyingOut) {
      result["varyingOut"] = *varyingOut;
    }
    return result;
  }


  std::string BindingType::toString() const {
    if (!elementType.has_value()) {
      return bindingTypeToString(type);
    }
    return fmt::format("{}<{}>", bindingTypeToString(type), bindingTypeToString(*elementType));
  }


  bool BindingType::allowedInTopLevel(Type type) {
    switch (type) {
#define TYPE(a, flags, allowed, vkT) \
  case Type::a:                      \
    return allowed;
#include "./ShaderReflectionTypes.def"
#undef TYPE
      default:
        return false;
    }
  }

  VkDescriptorType BindingType::toVkDescriptorType(Type type) {
    switch (type) {
#define TYPE(a, flags, allowed, vkT) \
  case Type::a:                      \
    return (VkDescriptorType)vkT;
#include "./ShaderReflectionTypes.def"
#undef TYPE
      default:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
  }

  VkDescriptorType BindingType::toVkDescriptorType() const {
    if (type == Type::ResourceArray && elementType.has_value()) {
      return toVkDescriptorType(*elementType);
    }
    return toVkDescriptorType(type);
  }

  json Node::toJson() const {
    json result;

    result["name"] = name;
    result["type"] = type.toString();  // bindingTypeToString(type);

    if (!meta.is_null()) {
      result["meta"] = meta;
    }

    // if (location.bindingIndex && location.bindingSet) {
    //   result["binding"] = fmt::format("{}.{}", *location.bindingSet, *location.bindingIndex);
    // }

    // if (location.byteSize) { result["size"] = *location.byteSize; }

    auto locationJson = location.toJson();
    if (!locationJson.is_null()) {
      result["location"] = locationJson;
    }

    if (members.size() > 0) {
      json memberArray = json::array();
      for (const auto* member : members) {
        if (member != nullptr) {
          memberArray.push_back(member->toJson());
        } else {
          // push null.
          memberArray.push_back(nullptr);
        }
      }
      result["members"] = memberArray;
    }


    return result;
  }

  using namespace slang;

  // ============================================================================
  // Helper Functions for Parsing
  // ============================================================================


  static ShaderReflection::Type mapDescriptorType(SpvReflectDescriptorType type) {
    using Type = ShaderReflection::Type;
    switch (type) {
      case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        return Type::UniformBuffer;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        return Type::StorageBuffer;
      case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return Type::Texture;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        return Type::Image;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
        return Type::Sampler;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        return Type::StorageImage;
      default:
        return Type::Unknown;
    }
  }

  // Recursively parse members of a block variable (struct, array, etc.)
  void ShaderReflection::parseBlockVariableMembers(const SpvReflectBlockVariable* var, Node* parent_node, Location loc) {
    if (!var || var->member_count == 0) {
      return;
    }


    for (uint32_t i = 0; i < var->member_count; ++i) {
      const auto* member = &var->members[i];
      if (!member || !member->name) {
        continue;
      }

      // Create location for this member with byte offset and size
      Location memberLoc = loc.child();
      memberLoc.byteOffset = member->offset;
      memberLoc.byteSize = member->size;

      // Check if this member is an array
      if (member->array.dims_count > 0) {
        u32 array_size = member->array.dims[0];
        // // Determine the element type of the array
        // BindingType element_type = Type::Unknown;
        // if (member->type_description) {
        //   element_type =
        //       mapDescriptorType(SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER);  // Default
        //       placeholder
        // }

        auto* n = newNode(Type::Array, member->name, memberLoc);
        parent_node->members.push_back(n);
      }
      // Check if this member is a struct type
      else if (member->type_description && member->type_description->op == SpvOpTypeStruct && member->member_count > 0) {
        // Create struct node with offset/size info
        auto* n = newNode(Type::Struct, member->name, memberLoc);
        parseBlockVariableMembers(member, n, memberLoc.child());
        parent_node->members.push_back(n);
      }
      // Regular field (scalar, vector, matrix, etc.)
      else {
        auto* n = newNode(Type::Scalar, member->name, memberLoc);
        parent_node->members.push_back(n);
      }
    }
  }

  // ============================================================================
  // ShaderReflection::parseFromSpirv Implementation
  // ============================================================================

  void ShaderReflection::parseFromSpirv(const u8* spirvData, size_t spirvSize) {
    // Create and parse the SPIR-V module
    SpvReflectShaderModule module = {};
    SpvReflectResult result = spvReflectCreateShaderModule(spirvSize, spirvData, &module);

    if (result != SPV_REFLECT_RESULT_SUCCESS) {
      std::cerr << "Failed to parse SPIR-V: " << result << std::endl;
      return;
    }



    auto root = newNode(Type::LogicalGroup, "Root", {/* empty on purpose */});

    // Parse descriptor bindings
    uint32_t binding_count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &binding_count, nullptr);

    if (binding_count > 0) {
      std::vector<SpvReflectDescriptorBinding*> bindings(binding_count);
      spvReflectEnumerateDescriptorBindings(&module, &binding_count, bindings.data());

      for (const auto* binding : bindings) {
        if (!binding || !binding->name) {
          continue;
        }

        Type type = mapDescriptorType(binding->descriptor_type);


        Location loc;
        loc.bindingIndex = binding->binding;
        loc.bindingSet = binding->set;
        loc.byteSize = binding->block.size;

        BindingType nodeType{type};
        u32 descriptorCount = 1;
        if (binding->array.dims_count > 0) {
          // Arrayed descriptor binding: descriptorCount is the product of the
          // dimensions. A runtime (unbounded) array reports a dimension of 0.
          nodeType = BindingType(Type::ResourceArray, type);
          descriptorCount = 1;
          for (u32 d = 0; d < binding->array.dims_count; ++d) {
            descriptorCount *= binding->array.dims[d];
          }
        }

        auto* node = newNode(nodeType, binding->name, loc);
        if (binding->array.dims_count > 0) {
          node->meta["elementCount"] = descriptorCount;
          if (descriptorCount == 0) {
            node->meta["unbounded"] = true;
          }
        }

        root->members.push_back(node);

        parseBlockVariableMembers(&binding->block, node, loc.child());
      }
    }



    // Parse push constant blocks
    for (uint32_t i = 0; i < module.push_constant_block_count; ++i) {
      const auto* pc_block = &module.push_constant_blocks[i];
      if (!pc_block || !pc_block->name) {
        continue;
      }

      Location loc;
      loc.pushConstant = true;
      loc.byteSize = pc_block->size;

      auto* node = newNode(PushConstant, pc_block->name, loc);

      // Parse push constant members (recursively handles nested structs and arrays)
      parseBlockVariableMembers(pc_block, node, loc.child());
      root->members.push_back(node);
    }



    // Now, we need to clean up the root node.
    // For example, the members of the `root` node might include something like
    // "material.albedo", and I would like that to be nested properly.  This
    // requires some post-processing of the root node to build the correct
    // hierarchy.

    std::map<std::string, Node*> namedTopLevel;
    std::vector<Node*> newMembers;

    for (auto* member : root->members) {
      namedTopLevel[member->name] = member;
    }

    for (auto* member : root->members) {
      std::string memberName = member->name;
      size_t dotPos = memberName.find('.');
      if (dotPos != std::string::npos) {
        std::string structName = memberName.substr(0, dotPos);
        std::string fieldName = memberName.substr(dotPos + 1);

        Node* structNode = nullptr;
        auto it = namedTopLevel.find(structName);
        if (it == namedTopLevel.end()) {
          structNode = newNode(Type::LogicalGroup, structName.c_str(), {/* empty on purpose */});
          namedTopLevel[structName] = structNode;
          newMembers.push_back(structNode);
        } else {
          structNode = it->second;
        }

        // Create field node
        auto* fieldNode = newNode(member->type, fieldName.c_str(), member->location);
        structNode->members.push_back(fieldNode);
      } else {
        newMembers.push_back(member);
      }
    }
    root->members = newMembers;

    // Clean up spirv-reflect module
    spvReflectDestroyShaderModule(&module);

    // Set root node
    this->root = mergeNodes(this->root, root);
    extractBindings();
  }




  // ============================================================================
  // ShaderReflection::parseFromSlang Implementation
  // ============================================================================




  template <typename T>
  static void applyOffset(OptionalInt<T>& base, size_t offset) {
    base = base ? static_cast<T>(*base + offset) : static_cast<T>(offset);
  }

  // Slang's layout model is relative: every VariableLayoutReflection stores
  // offsets relative to its enclosing scope, and the absolute location of a
  // parameter is the *sum* of the offsets along the entire access path. This
  // function applies one variable's offsets on top of the accumulated `loc`.
  bool adjustLocation(const char* debugName, ShaderReflection::Location& loc, slang::ParameterCategory layoutUnit, size_t offset, size_t space) {
    switch (layoutUnit) {
      case slang::ParameterCategory::DescriptorTableSlot:
        applyOffset(loc.bindingIndex, offset);
        applyOffset(loc.bindingSet, space);
        break;
      case slang::ParameterCategory::RegisterSpace:
      case slang::ParameterCategory::SubElementRegisterSpace:
        applyOffset(loc.bindingSet, offset);
        break;
      case slang::ParameterCategory::Uniform:
        applyOffset(loc.byteOffset, offset);
        break;
      case slang::ParameterCategory::PushConstantBuffer:
        loc.pushConstant = true;
        break;
      case slang::ParameterCategory::VaryingInput:
        applyOffset(loc.varyingIn, offset);
        break;
      case slang::ParameterCategory::VaryingOutput:
        applyOffset(loc.varyingOut, offset);
        break;

      default: {
        ren::warnln("Unhandled unit for variable '{}', layoutUnit={}, offset={}, space={}", debugName, static_cast<int>(layoutUnit), offset, space);
        break;
        // return false;
      }
    };
    return true;
  }

  bool adjustLocation(VariableLayoutReflection* vl, ShaderReflection::Location& loc) {
    if (!vl) {
      return false;
    }

    const char* debugName = vl->getName() ? vl->getName() : "<unnamed>";

    auto newLoc = loc;
    newLoc.stage = vl->getStage();

    int usedLayoutUnitCount = vl->getCategoryCount();
    for (int i = 0; i < usedLayoutUnitCount; ++i) {
      auto layoutUnit = vl->getCategoryByIndex(i);
      auto offset = vl->getOffset(layoutUnit);
      auto space = vl->getBindingSpace(layoutUnit);

      // SLANG_UNKNOWN_SIZE / SLANG_UNBOUNDED_SIZE mean the value depends on
      // unresolved generics or link-time constants; don't fold them in.
      if (offset >= SLANG_UNKNOWN_SIZE || space >= SLANG_UNKNOWN_SIZE) {
        ren::warnln("Variable '{}' has an unresolved offset for layoutUnit={}, skipping", debugName, static_cast<int>(layoutUnit));
        continue;
      }

      if (!adjustLocation(debugName, newLoc, layoutUnit, offset, space)) {
        return false;
      };
    }

    // Update location
    loc = newLoc;

    return true;
  }


  bool ShaderReflection::Location::adjustLocation(slang::VariableLayoutReflection* vl) { return ren::adjustLocation(vl, *this); }


  ShaderReflection::Type mapSlangType(SlangResourceShape shape, SlangResourceAccess access) {
    using Type = ShaderReflection::Type;
    switch (shape) {
      case SlangResourceShape::SLANG_STRUCTURED_BUFFER:
      case SlangResourceShape::SLANG_BYTE_ADDRESS_BUFFER:
        return Type::StorageBuffer;
      default:
        return Type::Unknown;
    }
  }

  static ShaderReflection::Type mapSlangBindingType(slang::BindingType bindingType) {
    using Type = ShaderReflection::Type;
    switch (bindingType) {
      case slang::BindingType::Sampler:
        return Type::Sampler;
      case slang::BindingType::Texture:
        return Type::Image;
      case slang::BindingType::CombinedTextureSampler:
        return Type::Texture;
      case slang::BindingType::MutableTexture:
        return Type::StorageImage;
      case slang::BindingType::ConstantBuffer:
        return Type::UniformBuffer;
      case slang::BindingType::ParameterBlock:
        return Type::ParameterBlock;
      case slang::BindingType::TypedBuffer:
      case slang::BindingType::RawBuffer:
      case slang::BindingType::MutableTypedBuffer:
      case slang::BindingType::MutableRawBuffer:
        return Type::StorageBuffer;
      default:
        return Type::Unknown;
    }
  }

  // Determine the engine resource type for a type layout by consulting its
  // binding ranges. Binding ranges see through arrays and report the leaf
  // descriptor type, so this works for both `Texture2D` and `Texture2D[4]`.
  static ShaderReflection::Type resourceTypeFromTypeLayout(TypeLayoutReflection* tl) {
    using Type = ShaderReflection::Type;
    auto rangeCount = tl->getBindingRangeCount();
    for (SlangInt i = 0; i < rangeCount; ++i) {
      Type t = mapSlangBindingType(tl->getBindingRangeType(i));
      if (t != Type::Unknown) {
        return t;
      }
    }
    if (auto type = tl->getType()) {
      return mapSlangType(type->getResourceShape(), type->getResourceAccess());
    }
    return Type::Unknown;
  }




  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  struct DescriptorBinding {
    uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    uint32_t count = 0;
    VkShaderStageFlags stageFlags = 0;
  };

  struct DescriptorSetLayoutInfo {
    uint32_t set;
    std::vector<DescriptorBinding> bindings;
  };

  struct ProgramDescriptorLayout {
    std::vector<DescriptorSetLayoutInfo> sets;
  };

  static VkDescriptorType slangBindingTypeToVk(slang::BindingType bindingType) {
    switch (bindingType) {
      case slang::BindingType::Sampler:
        return VK_DESCRIPTOR_TYPE_SAMPLER;
      case slang::BindingType::CombinedTextureSampler:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      case slang::BindingType::Texture:
        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      case slang::BindingType::MutableTexture:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
      case slang::BindingType::TypedBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
      case slang::BindingType::MutableTypedBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
      case slang::BindingType::RawBuffer:
      case slang::BindingType::MutableRawBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      case slang::BindingType::ConstantBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      case slang::BindingType::InputRenderTarget:
        return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
      case slang::BindingType::InlineUniformData:
        return VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
      case slang::BindingType::RayTracingAccelerationStructure:
        return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
      default:
        return VK_DESCRIPTOR_TYPE_MAX_ENUM;  // Unknown/unsupported
    }
  }

  // Walk a type layout's descriptor sets and record every descriptor range at
  // its absolute (set, binding) location. `spaceOffset` is the accumulated
  // register space of the enclosing scope; each descriptor set's actual space
  // is that plus getDescriptorSetSpaceOffset() (the set index passed to the
  // getDescriptorSet* functions is an index into the type layout's list of
  // sets, *not* a space number). The contents of ParameterBlock sub-objects
  // are not part of this type layout's ranges and are reached by recursing
  // through the sub-object ranges with their space offsets.
  static void extractBindingsFromTypeLayout(slang::TypeLayoutReflection* typeLayout, uint32_t spaceOffset, VkShaderStageFlags stageFlags,
                                            std::unordered_map<uint32_t, std::unordered_map<uint32_t, DescriptorBinding>>& setBindings) {
    if (!typeLayout) {
      return;
    }

    auto setCount = typeLayout->getDescriptorSetCount();
    for (SlangInt setIndex = 0; setIndex < setCount; ++setIndex) {
      auto relativeSpace = typeLayout->getDescriptorSetSpaceOffset(setIndex);
      if (static_cast<size_t>(relativeSpace) >= SLANG_UNKNOWN_SIZE) {
        continue;
      }
      uint32_t space = spaceOffset + static_cast<uint32_t>(relativeSpace);

      auto rangeCount = typeLayout->getDescriptorSetDescriptorRangeCount(setIndex);
      for (SlangInt rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex) {
        // Only DescriptorTableSlot ranges are real Vulkan descriptors; push
        // constant and specialization-constant ranges are not.
        if (typeLayout->getDescriptorSetDescriptorRangeCategory(setIndex, rangeIndex) != slang::ParameterCategory::DescriptorTableSlot) {
          continue;
        }

        VkDescriptorType vkType = slangBindingTypeToVk(typeLayout->getDescriptorSetDescriptorRangeType(setIndex, rangeIndex));
        if (vkType == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
          continue;
        }

        auto bindingIndex = typeLayout->getDescriptorSetDescriptorRangeIndexOffset(setIndex, rangeIndex);
        if (static_cast<size_t>(bindingIndex) >= SLANG_UNKNOWN_SIZE) {
          continue;
        }
        auto descriptorCount = typeLayout->getDescriptorSetDescriptorRangeDescriptorCount(setIndex, rangeIndex);
        // Unbounded ranges get a count of 0; a real cap must be chosen by the
        // consumer (e.g. from a device limit) when building the set layout.
        uint32_t count = static_cast<size_t>(descriptorCount) >= SLANG_UNKNOWN_SIZE ? 0 : static_cast<uint32_t>(descriptorCount);

        auto& binding = setBindings[space][static_cast<uint32_t>(bindingIndex)];
        if (binding.type == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
          binding.binding = static_cast<uint32_t>(bindingIndex);
          binding.type = vkType;
          binding.count = count;
          binding.stageFlags = stageFlags;
        } else {
          // Merge stage flags if binding already exists
          binding.stageFlags |= stageFlags;
        }
      }
    }

    // Recurse into sub-objects. ParameterBlocks contribute their descriptor
    // sets in their own register space.
    auto subObjectCount = typeLayout->getSubObjectRangeCount();
    for (SlangInt subObjectIndex = 0; subObjectIndex < subObjectCount; ++subObjectIndex) {
      auto bindingRangeIndex = typeLayout->getSubObjectRangeBindingRangeIndex(subObjectIndex);
      if (typeLayout->getBindingRangeType(bindingRangeIndex) != slang::BindingType::ParameterBlock) {
        continue;
      }
      auto subSpace = typeLayout->getSubObjectRangeSpaceOffset(subObjectIndex);
      if (static_cast<size_t>(subSpace) >= SLANG_UNKNOWN_SIZE) {
        continue;
      }
      auto* leaf = typeLayout->getBindingRangeLeafTypeLayout(bindingRangeIndex);
      // A ParameterBlock type layout lists itself as one of its own
      // sub-object ranges; don't recurse into it forever.
      if (leaf == typeLayout) {
        continue;
      }
      extractBindingsFromTypeLayout(leaf, spaceOffset + static_cast<uint32_t>(subSpace), stageFlags, setBindings);
    }
  }

  ProgramDescriptorLayout extractDescriptorLayout(slang::ProgramLayout* programLayout) {
    ProgramDescriptorLayout result;

    // Map: set index -> (binding index -> DescriptorBinding)
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, DescriptorBinding>> setBindings;

    // Extract from global scope parameters
    auto globalScope = programLayout->getGlobalParamsTypeLayout();
    extractBindingsFromTypeLayout(globalScope, 0, VK_SHADER_STAGE_ALL, setBindings);

    // Extract from each entry point
    auto entryPointCount = programLayout->getEntryPointCount();
    for (SlangInt ep = 0; ep < entryPointCount; ++ep) {
      auto entryPoint = programLayout->getEntryPointByIndex(ep);
      auto stage = entryPoint->getStage();

      VkShaderStageFlags vkStage = 0;
      switch (stage) {
        case SLANG_STAGE_VERTEX:
          vkStage = VK_SHADER_STAGE_VERTEX_BIT;
          break;
        case SLANG_STAGE_FRAGMENT:
          vkStage = VK_SHADER_STAGE_FRAGMENT_BIT;
          break;
        case SLANG_STAGE_COMPUTE:
          vkStage = VK_SHADER_STAGE_COMPUTE_BIT;
          break;
        case SLANG_STAGE_GEOMETRY:
          vkStage = VK_SHADER_STAGE_GEOMETRY_BIT;
          break;
        case SLANG_STAGE_HULL:
          vkStage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
          break;
        case SLANG_STAGE_DOMAIN:
          vkStage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
          break;
        case SLANG_STAGE_RAY_GENERATION:
          vkStage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
          break;
        case SLANG_STAGE_CLOSEST_HIT:
          vkStage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
          break;
        case SLANG_STAGE_MISS:
          vkStage = VK_SHADER_STAGE_MISS_BIT_KHR;
          break;
        case SLANG_STAGE_ANY_HIT:
          vkStage = VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
          break;
        case SLANG_STAGE_INTERSECTION:
          vkStage = VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
          break;
        case SLANG_STAGE_CALLABLE:
          vkStage = VK_SHADER_STAGE_CALLABLE_BIT_KHR;
          break;
        case SLANG_STAGE_MESH:
          vkStage = VK_SHADER_STAGE_MESH_BIT_EXT;
          break;
        case SLANG_STAGE_AMPLIFICATION:
          vkStage = VK_SHADER_STAGE_TASK_BIT_EXT;
          break;
        default:
          vkStage = VK_SHADER_STAGE_ALL;
          break;
      }

      auto entryPointLayout = entryPoint->getTypeLayout();
      extractBindingsFromTypeLayout(entryPointLayout, 0, vkStage, setBindings);
    }

    // Convert map to sorted vector structure
    for (auto& [setIndex, bindings] : setBindings) {
      DescriptorSetLayoutInfo setInfo;
      setInfo.set = setIndex;

      for (auto& [bindingIndex, binding] : bindings) {
        setInfo.bindings.push_back(binding);
      }

      // Sort bindings by index
      std::sort(setInfo.bindings.begin(), setInfo.bindings.end(),
                [](const DescriptorBinding& a, const DescriptorBinding& b) { return a.binding < b.binding; });

      result.sets.push_back(std::move(setInfo));
    }

    // Sort sets by index
    std::sort(result.sets.begin(), result.sets.end(),
              [](const DescriptorSetLayoutInfo& a, const DescriptorSetLayoutInfo& b) { return a.set < b.set; });

    return result;
  }
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


  Node* ShaderReflection::extractVariableLayout(VariableLayoutReflection* vl, TypeLayoutReflection* tl, Location loc) {
    if (!tl) {
      return nullptr;
    }

    if (vl) {
      if (adjustLocation(vl, loc) == false) {
        return nullptr;
      }
    }



    const char* name = "";

    if (vl && vl->getName()) {
      name = vl->getName();
    }


    size_t uniformSize = tl->getSize(slang::ParameterCategory::Uniform);
    if (uniformSize < SLANG_UNKNOWN_SIZE) {
      loc.byteSize = static_cast<u32>(uniformSize);
    }

    using Kind = slang::TypeReflection::Kind;
    auto kind = tl->getKind();

    // ren::println("name={} kind={} size={}", name, static_cast<int>(kind), uniformSize);
    switch (kind) {
      case Kind::ParameterBlock: {
        // A parameter block introduces its own register space (descriptor
        // set). The space offset arrived through the SubElementRegisterSpace
        // category of the variable itself (already applied to loc above), but
        // binding indices restart inside the new space, so any accumulated
        // index from the enclosing scope must not leak in.
        Location blockLoc = loc.child();
        blockLoc.bindingIndex = {};

        // The container var layout describes the implicit constant buffer the
        // block allocates when its element type has ordinary uniform data.
        // Container and element offsets both apply relative to the same base.
        Location containerLoc = blockLoc.child();
        if (auto cvl = tl->getContainerVarLayout()) {
          ren::adjustLocation(cvl, containerLoc);
        }

        // The element var layout's offsets (e.g. the descriptor slot skip past
        // the implicit constant buffer) pair with *its own* type layout, whose
        // field offsets are relative. tl->getElementTypeLayout() returns a
        // layout with absolute field offsets and would double-count.
        auto evl = tl->getElementVarLayout();
        auto etl = evl ? evl->getTypeLayout() : tl->getElementTypeLayout();

        // TODO: we need to handle the case where the parameter block is more than just a collection
        // of bindings (that is, it includes fields.)
        auto* node = extractVariableLayout(evl, etl, blockLoc.child());
        if (!node) {
          node = newNode(Type::ParameterBlock, name, blockLoc);
        }
        node->name = name;
        node->type = Type::ParameterBlock;
        // The block node itself represents the implicit constant buffer (if
        // any): bindingIndex stays unset when the element has no uniform data.
        node->location.bindingIndex = containerLoc.bindingIndex;
        node->location.bindingSet = containerLoc.bindingSet;

        return node;
      }

      case Kind::ConstantBuffer: {
        // The buffer's own descriptor slot (or push constant range) lives on
        // the container var layout, applied on top of the variable's offsets
        // which are already accumulated into loc.
        Location containerLoc = loc.child();
        if (auto cvl = tl->getContainerVarLayout()) {
          ren::adjustLocation(cvl, containerLoc);
        }

        // See the ParameterBlock case: pair the element var layout with its
        // own type layout so relative offsets are not double-counted.
        auto evl = tl->getElementVarLayout();
        auto etl = evl ? evl->getTypeLayout() : tl->getElementTypeLayout();

        auto* node = extractVariableLayout(evl, etl, loc.child());
        if (!node) {
          node = newNode(Type::UniformBuffer, name, containerLoc);
        }

        node->name = name;
        node->location.bindingIndex = containerLoc.bindingIndex;
        node->location.bindingSet = containerLoc.bindingSet;

        if (containerLoc.pushConstant) {
          node->type = PushConstant;
        } else {
          Type bindingType = Type::UniformBuffer;
          node->type = bindingType;
        }

        return node;
      }



      case Kind::Array: {
        size_t elementCount = tl->getElementCount();
        // SLANG_UNBOUNDED_SIZE means `T foo[]`; SLANG_UNKNOWN_SIZE means the
        // count depends on unresolved generics. Either way the elements cannot
        // be enumerated.
        const bool unbounded = elementCount >= SLANG_UNKNOWN_SIZE;
        auto evl = tl->getElementVarLayout();
        auto etl = tl->getElementTypeLayout();


        Type nodeType = Type::Array;
        Type elementType = Type::Unknown;
        if (etl) {
          auto elementKind = etl->getKind();

          switch (elementKind) {
            case Kind::Struct: {
              nodeType = Type::Array;
              elementType = Type::Struct;
              break;
            }
            case Kind::Scalar:
            case Kind::Vector:
            case Kind::Matrix: {
              nodeType = Type::Array;
              elementType = Type::Scalar;
              break;
            }
            default: {
              // Resources, samplers, constant buffers, nested arrays of
              // resources: the array's own binding ranges report the leaf
              // descriptor type.
              elementType = resourceTypeFromTypeLayout(tl);
              if (elementType != Type::Unknown) {
                nodeType = Type::ResourceArray;
              } else {
                ren::warnln("Array element kind not being handled correctly yet! elementKind={}", static_cast<int>(elementKind));
              }
              break;
            }
          }
        }

        BindingType t = elementType == Type::Unknown ? BindingType(nodeType) : BindingType(nodeType, elementType);
        auto* node = newNode(t, name, loc);
        // Record the element count so binding extraction doesn't have to
        // infer it from enumerated children (0 means unbounded).
        node->meta["elementCount"] = unbounded ? 0 : static_cast<u64>(elementCount);

        if (unbounded) {
          node->meta["unbounded"] = true;
          return node;
        }

        size_t stride = tl->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
        if (stride >= SLANG_UNKNOWN_SIZE) {
          stride = 0;
        }

        for (u32 i = 0; i < elementCount; ++i) {
          auto l = loc.child();
          l.arrayIndex = i;
          if (stride > 0) {
            applyOffset(l.byteOffset, i * stride);
          }
          auto n = extractVariableLayout(evl, etl, l);
          if (!n) {
            continue;
          }
          n->name = fmt::format("{}", i);
          node->members.push_back(n);
        }


        return node;
      }
      case Kind::Matrix:
      case Kind::Vector:
      case Kind::Scalar: {
        size_t sizeInBytes = tl->getSize();
        if (sizeInBytes < SLANG_UNKNOWN_SIZE) {
          loc.byteSize = static_cast<u32>(sizeInBytes);
        }
        auto* fieldNode = newNode(Scalar, name, loc);
        return fieldNode;
      }

      case Kind::Pointer: {
        size_t sizeInBytes = tl->getSize();
        if (sizeInBytes < SLANG_UNKNOWN_SIZE) {
          loc.byteSize = static_cast<u32>(sizeInBytes);
        }
        auto* fieldNode = newNode(Pointer, name, loc);
        return fieldNode;
      }

      case Kind::Struct: {
        // TODO: store the Location!

        auto* structNode = newNode(Struct, name, loc);

        u32 fieldCount = tl->getFieldCount();
        for (u32 i = 0; i < fieldCount; ++i) {
          auto fieldVarLayout = tl->getFieldByIndex(i);
          auto fieldTypeLayout = fieldVarLayout->getTypeLayout();

          auto* fieldNode = extractVariableLayout(fieldVarLayout, fieldTypeLayout, loc.child());
          structNode->members.push_back(fieldNode);
        }

        return structNode;
      }


      case Kind::SamplerState: {
        auto* samplerNode = newNode(Sampler, name, loc);
        return samplerNode;
      }

      case Kind::Resource: {
        Type resourceType = resourceTypeFromTypeLayout(tl);
        if (resourceType == Type::Unknown) {
          ren::errln("resource {} has unhandled binding/shape", name);
        }

        auto* resourceNode = newNode(resourceType, name, loc);


        u32 fieldCount = tl->getFieldCount();
        for (u32 i = 0; i < fieldCount; ++i) {
          auto fieldVarLayout = tl->getFieldByIndex(i);
          auto fieldTypeLayout = fieldVarLayout->getTypeLayout();

          auto* fieldNode = extractVariableLayout(fieldVarLayout, fieldTypeLayout, loc.child());
          resourceNode->members.push_back(fieldNode);
        }

        return resourceNode;
      }

      default: {
        ren::warnln("Unhandled kind: {}", static_cast<int>(kind));
        break;
      }
    }

    return nullptr;
  }

  void ShaderReflection::parseFromSlang(ProgramLayout* programLayout, bool dumpDebugInfo) {
    if (!programLayout) {
      std::cerr << "Invalid ProgramLayout pointer" << std::endl;
      return;
    }
    if (dumpDebugInfo) {
      Slang::ComPtr<slang::IBlob> jsonBlob;
      programLayout->toJson(jsonBlob.writeRef());

      json j = json::parse(std::string_view((const char*)jsonBlob->getBufferPointer(), jsonBlob->getBufferSize()));
      ren::println("{}", j.dump(2));
      ren::ReflectingPrinting printer;
      printer.printProgramLayout(programLayout, SlangCompileTarget::SLANG_SPIRV);
    }

    // Create root node
    auto root = newNode(Type::LogicalGroup, "Root", {/* empty on purpose */});

    // Parse global parameters
    auto globalParams = programLayout->getGlobalParamsVarLayout();
    if (globalParams) {
      auto globalTypeLayout = globalParams->getTypeLayout();
      if (globalTypeLayout) {
        auto globalKind = globalTypeLayout->getKind();
        if (globalKind == slang::TypeReflection::Kind::ConstantBuffer || globalKind == slang::TypeReflection::Kind::ParameterBlock) {
          // Bare global uniforms make slang wrap the entire global scope in an
          // implicit constant buffer ($Globals). Extract it as an explicit
          // node so its binding and the parameters inside it are not lost.
          ShaderReflection::Location loc;
          auto* node = extractVariableLayout(globalParams, globalTypeLayout, loc);
          if (node) {
            if (node->name.empty()) {
              node->name = "$Globals";
            }
            if (!BindingType::allowedInTopLevel(node->type.type)) {
              ren::warnln("Unsupported top-level global variable '{}', type={}", node->name, node->type.toString());
            }
            root->members.push_back(node);
          }
        } else {
          // Traverse global scope (usually a struct containing all globals)
          u32 fieldCount = globalTypeLayout->getFieldCount();
          for (u32 i = 0; i < fieldCount; ++i) {
            auto fieldVarLayout = globalTypeLayout->getFieldByIndex(i);
            if (fieldVarLayout) {
              auto fieldTypeLayout = fieldVarLayout->getTypeLayout();
              if (fieldTypeLayout) {
                // Create a fresh location.
                ShaderReflection::Location loc;
                auto* node = extractVariableLayout(fieldVarLayout, fieldTypeLayout, loc);
                if (node) {
                  // There's a little check we have to do here - we only want to
                  // support *some* types at the top level, to simplify the shader resource management
                  // system down the line.
                  if (!BindingType::allowedInTopLevel(node->type.type)) {
                    ren::warnln("Unsupported top-level global variable '{}', type={}", node->name, node->type.toString());
                  }

                  root->members.push_back(node);
                }
              }
            }
          }
        }
      }
    }

    // Parse entry-point-specific parameters
    u32 entryPointCount = programLayout->getEntryPointCount();
    for (u32 epIndex = 0; epIndex < entryPointCount; ++epIndex) {
      auto entryPoint = programLayout->getEntryPointByIndex(epIndex);
      if (!entryPoint) {
        continue;
      }


      // Parse entry point parameters
      auto entryParams = entryPoint->getVarLayout();
      if (!entryParams) {
        continue;
      }

      auto entryTypeLayout = entryParams->getTypeLayout();
      if (!entryTypeLayout) {
        continue;
      }


      Location loc;
      adjustLocation(entryParams, loc);
      auto epRoot = newNode(Type::EntryPoint, entryPoint->getName(), loc);
      root->members.push_back(epRoot);

      u32 fieldCount = entryTypeLayout->getFieldCount();
      for (u32 i = 0; i < fieldCount; ++i) {
        auto fieldVarLayout = entryTypeLayout->getFieldByIndex(i);
        if (!fieldVarLayout) {
          continue;
        }
        auto fieldTypeLayout = fieldVarLayout->getTypeLayout();
        if (!fieldTypeLayout) {
          continue;
        }
        // Create fresh AccessPath for each entry point parameter
        auto* childNode = extractVariableLayout(fieldVarLayout, fieldTypeLayout, loc.child());
        if (childNode) {
          epRoot->members.push_back(childNode);
        }
      }
    }

    // Set root node
    this->root = mergeNodes(this->root, root);

    auto info = extractDescriptorLayout(programLayout);

    for (u64 set = 0; set < info.sets.size(); ++set) {
      auto& setInfo = info.sets[set];
      for (int bindingIdx = 0; bindingIdx < static_cast<int>(setInfo.bindings.size()); ++bindingIdx) {
        auto& binding = setInfo.bindings[bindingIdx];

        ren::dbgln("Set {} Binding {}: type={} count={} stages=0x{:X}", setInfo.set, binding.binding, static_cast<int>(binding.type), binding.count,
                   binding.stageFlags);
      }
    }
    extractBindings();
  }


  static bool hasRealBinding(const Node* node) {
    // special case ParameterBlock which has a byte size! They need an implicit uniform buffer binding.
    if (node->type.type == ShaderReflection::Type::ParameterBlock && node->location.byteSize && *node->location.byteSize > 0) {
      return true;
    }


    switch (node->type.type) {
#define TYPE(a, flags, ...)       \
  case ShaderReflection::Type::a: \
    return (flags & SRT_RESOURCE) ? true : false;
#include "./ShaderReflectionTypes.def"
#undef TYPE
      default:
        return false;
    }
  }


  void ShaderReflection::extractBindings(void) {
    REN_PROFILE_SCOPE("ShaderReflection::extractBindings");
    bindings.clear();
    if (!root) {
      return;
    }


    std::map<u32, std::map<u32, Binding>> bindingMap;
    auto ensureBinding = [&](u32 set, u32 index) -> Binding& {
      auto& b = bindingMap[set][index];
      b.set = set;
      b.index = index;
      return b;
    };

    std::function<void(Node*, const std::string&)> traverse;
    traverse = [&](Node* node, const std::string& path) {
      REN_PROFILE_SCOPE("traverse");
      if (!node) {
        return;
      }

      std::string currentPath = path.empty() ? node->name : path + "." + node->name;

      if (node->location.bindingSet && node->location.bindingIndex && hasRealBinding(node)) {
        auto& binding = ensureBinding(*node->location.bindingSet, *node->location.bindingIndex);
        if (node->location.arrayIndex) {
          // Array elements share their array's binding: refine the count but
          // don't clobber the entry the array node itself claimed.
          binding.count = std::max(binding.count, *node->location.arrayIndex + 1);
          if (binding.path.empty()) {
            binding.type = node->type;
            binding.path = currentPath;
          }
        } else {
          binding.type = node->type;
          binding.path = currentPath;
          if (node->meta.contains("elementCount")) {
            // Resource arrays know their descriptor count up front; 0 means
            // an unbounded array.
            binding.count = node->meta["elementCount"].get<u32>();
          }
        }
      }

      // Recurse into members
      for (auto* member : node->members) {
        traverse(member, currentPath);
      }
    };

    for (auto* member : root->members) {
      traverse(member, "");
    }

    for (auto& [set, bindingsInSet] : bindingMap) {
      for (auto& [index, binding] : bindingsInSet) {
        bindings.push_back(binding);
      }
    }

    // Sort bindings by set, then by index
    std::sort(bindings.begin(), bindings.end(), [](const Binding& a, const Binding& b) {
      if (a.set != b.set) {
        return a.set < b.set;
      }
      return a.index < b.index;
    });


    ren::dbgln("Extracted {} bindings:", bindings.size());
    for (auto& binding : bindings) {
      ren::dbgln("- {}.{} [{}] = {} ({})", binding.set, binding.index, binding.count, binding.path, binding.type.toString());
    }
  }

  // Helper function to check if two Location objects are compatible
  static bool locationsAreCompatible(const ShaderReflection::Location& locA, const ShaderReflection::Location& locB) {
    return locA.pushConstant == locB.pushConstant && locA.bindingSet == locB.bindingSet && locA.bindingIndex == locB.bindingIndex &&
           locA.byteOffset == locB.byteOffset && locA.byteSize == locB.byteSize && locA.arrayIndex == locB.arrayIndex &&
           locA.varyingIn == locB.varyingIn && locA.varyingOut == locB.varyingOut;
  }


  Node* ShaderReflection::newNode(BindingType type, const char* name, const Location& location) {
    Node* node = new Node();
    node->type = type;
    node->name = name;
    node->location = location;
    allNodes.emplace_back(node);
    return node;
  }

  Node* ShaderReflection::mergeNodes(Node* nodeA, Node* nodeB) {
    if (!nodeA) {
      return nodeB;
    }
    if (!nodeB) {
      return nodeA;
    }

    std::map<std::string, Node*> membersA;
    for (auto* member : nodeA->members) {
      membersA[member->name] = member;
    }

    std::set<std::string> memberNamesB;
    std::vector<Node*> membersToAdd;

    for (auto* memberB : nodeB->members) {
      memberNamesB.insert(memberB->name);

      auto it = membersA.find(memberB->name);
      if (it != membersA.end()) {
        auto* memberA = it->second;

        if (memberA->type != memberB->type) {
          throw std::runtime_error(fmt::format("Shader merge conflict: '{}' has incompatible types ({} vs {})", memberB->name,
                                               memberA->type.toString(), memberB->type.toString()));
        }

        if (!locationsAreCompatible(memberA->location, memberB->location)) {
          throw std::runtime_error(fmt::format("Shader merge conflict: '{}' has incompatible location info", memberB->name));
        }

        // If both are container types, recursively merge their members
        if (memberA->type == Type::LogicalGroup || memberA->type == Type::Struct || memberA->type == Type::ParameterBlock) {
          mergeNodes(memberA, memberB);
        }
      } else {
        // Member only exists in nodeB, we'll add it
        membersToAdd.push_back(memberB);
      }
    }

    // Add members that only exist in nodeB
    for (auto* member : membersToAdd) {
      nodeA->members.push_back(member);
    }

    nodeA->meta["merged"] = true;

    return nodeA;
  }


  json ShaderReflection::toJson() const {
    json j;

    if (root) {
      j["root"] = *root;
    }


    j["bindings"] = json::array();

    for (const auto& b : bindings) {
      j["bindings"].push_back(b);
    }

    return j;
  }




}  // namespace ren
