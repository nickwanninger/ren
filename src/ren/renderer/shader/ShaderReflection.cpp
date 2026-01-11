#include "./ShaderReflection.h"
#include <unordered_map>
#include <string>
#include <set>

#include <slang-com-ptr.h>
#include <slang.h>
#include <imgui/imgui.h>

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
        if (binding->array.dims_count > 0) {
          // TODO: handle this? Idk.
          u32 array_size = binding->array.dims[0];
        }


        auto* node = newNode(type, binding->name, loc);

        root->members.push_back(node);

        // Handle array bindings
        if (binding->array.dims_count > 0) {
          u32 array_size = binding->array.dims[0];
        } else if (type == UniformBuffer || type == StorageBuffer) {
        }
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
  static void applyOffset(OptionalInt<T>& base, u32 offset) {
    base = base ? *base + offset : offset;
  }

  bool adjustLocation(const char* debugName, ShaderReflection::Location& loc, slang::ParameterCategory layoutUnit, u32 offset, u32 space) {
    switch (layoutUnit) {
      case slang::ParameterCategory::DescriptorTableSlot:
        // applyOffset(loc.bindingIndex, offset);
        applyOffset(loc.bindingSet, space);
        break;
      case slang::ParameterCategory::SubElementRegisterSpace:
        // applyOffset(loc.bindingIndex, 0);
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
    // newLoc.rangeIndexInSet = vl->getBindingIndex();
    newLoc.bindingIndex = vl->getBindingIndex();

    int usedLayoutUnitCount = vl->getCategoryCount();
    for (int i = 0; i < usedLayoutUnitCount; ++i) {
      auto layoutUnit = vl->getCategoryByIndex(i);
      auto offset = vl->getOffset(layoutUnit);
      auto space = vl->getBindingSpace(layoutUnit);

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




  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

  struct DescriptorBinding {
    uint32_t binding;
    VkDescriptorType type;
    uint32_t count;
    VkShaderStageFlags stageFlags;
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

  static void extractBindingsFromTypeLayout(slang::TypeLayoutReflection* typeLayout, VkShaderStageFlags stageFlags,
                                            std::unordered_map<uint32_t, std::unordered_map<uint32_t, DescriptorBinding>>& setBindings) {
    if (!typeLayout) {
      return;
    }

    auto bindingRangeCount = typeLayout->getBindingRangeCount();

    for (SlangInt i = 0; i < bindingRangeCount; ++i) {
      auto bindingType = typeLayout->getBindingRangeType(i);

      // Skip non-descriptor bindings (e.g., push constants, varying)
      VkDescriptorType vkType = slangBindingTypeToVk(bindingType);
      if (vkType == VK_DESCRIPTOR_TYPE_MAX_ENUM) {
        continue;
      }

      auto bindingCount = typeLayout->getBindingRangeBindingCount(i);
      auto descriptorSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(i);
      auto firstDescriptorIndex = typeLayout->getBindingRangeFirstDescriptorRangeIndex(i);

      // Get the actual binding index from the descriptor range
      auto descRangeCount = typeLayout->getDescriptorSetDescriptorRangeCount(descriptorSetIndex);

      // For each descriptor range in this binding range
      auto rangeIndexInSet = firstDescriptorIndex;
      if (rangeIndexInSet < descRangeCount) {
        auto bindingIndex = typeLayout->getDescriptorSetDescriptorRangeIndexOffset(descriptorSetIndex, rangeIndexInSet);

        auto& binding = setBindings[descriptorSetIndex][bindingIndex];
        if (binding.type == VkDescriptorType{}) {
          binding.binding = static_cast<uint32_t>(bindingIndex);
          binding.type = vkType;
          binding.count = static_cast<uint32_t>(bindingCount);
          binding.stageFlags = stageFlags;
        } else {
          // Merge stage flags if binding already exists
          binding.stageFlags |= stageFlags;
        }
      }
    }
  }

  ProgramDescriptorLayout extractDescriptorLayout(slang::ProgramLayout* programLayout) {
    ProgramDescriptorLayout result;

    // Map: set index -> (binding index -> DescriptorBinding)
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, DescriptorBinding>> setBindings;

    // Extract from global scope parameters
    auto globalScope = programLayout->getGlobalParamsTypeLayout();
    extractBindingsFromTypeLayout(globalScope, VK_SHADER_STAGE_ALL, setBindings);

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
      extractBindingsFromTypeLayout(entryPointLayout, vkStage, setBindings);
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
    // if (!vl) return nullptr;
    // if (!tl) return nullptr;


    if (vl && tl) {
      if (adjustLocation(vl, loc) == false) {
        return nullptr;
      }
    }



    const char* name = "";

    if (vl && vl->getName()) {
      name = vl->getName();
    }


    size_t uniformSize = tl->getSize(slang::ParameterCategory::Uniform);
    loc.byteSize = uniformSize;

    using Kind = slang::TypeReflection::Kind;
    auto kind = tl->getKind();

    // ren::println("name={} kind={} size={}", name, static_cast<int>(kind), uniformSize);
    switch (kind) {
      case Kind::ParameterBlock: {
        auto evl = tl->getElementVarLayout();
        auto etl = tl->getElementTypeLayout();

        // TODO: we need to handle the case where the parameter block is more than just a collection
        // of bindings (that is, it includes fields.)
        auto* node = extractVariableLayout(evl, etl, loc.child());
        node->name = name;
        node->type = Type::ParameterBlock;

        return node;
      }

      case Kind::ConstantBuffer: {
        auto evl = tl->getElementVarLayout();
        auto etl = tl->getElementTypeLayout();

        auto* node = extractVariableLayout(evl, etl, loc.child());


        node->name = name;

        if (loc.pushConstant) {
          node->type = PushConstant;
        } else {
          Type bindingType = Type::UniformBuffer;
          node->type = bindingType;
        }

        return node;
      }



      case Kind::Array: {
        // ren::println("Array!");
        auto elementCount = tl->getElementCount();
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
            case Kind::Resource: {
              nodeType = Type::ResourceArray;
              auto type = etl->getType();
              auto shape = type->getResourceShape();
              auto access = type->getResourceAccess();
              elementType = mapSlangType(shape, access);
              break;
            }
            case Kind::SamplerState: {
              elementType = Type::Sampler;
              break;
            }
            default: {
              ren::warnln("Array element kind not being handled correctly yet! elementKind={}", static_cast<int>(elementKind));
              elementType = Type::Unknown;
              break;
            }
          }
        }

        BindingType t = elementType == Type::Unknown ? BindingType(nodeType) : BindingType(nodeType, elementType);
        auto* node = newNode(t, name, loc);


        // go over element?


        u32 stride = tl->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
        // ren::println(
        //     "Array detected: name='{}', elementCount={}, elementType={}, evtl:{},{}, stride={}",
        //     name, elementCount, static_cast<int>(elementType), (void*)evl, (void*)etl, stride);




        for (u32 i = 0; i < elementCount; ++i) {
          auto l = loc.child();
          l.arrayIndex = i;
          if (stride > 0) {
            applyOffset(l.byteOffset, i * stride);
          }
          auto n = extractVariableLayout(evl, etl, l);
          n->name = fmt::format("{}", i);
          // ren::println("Array element {} extracted: {}", i, (void*)n);
          node->members.push_back(n);
        }


        return node;
      }
      case Kind::Matrix:
      case Kind::Vector:
      case Kind::Scalar: {
        auto sizeInBytes = tl->getSize();
        loc.byteSize = sizeInBytes;
        auto* fieldNode = newNode(Scalar, name, loc);
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
        auto type = tl->getType();
        auto shape = type->getResourceShape();
        auto access = type->getResourceAccess();

        // TODO: unify this analysis!
        Type resourceType = mapSlangType(shape, access);

        auto rangeCount = tl->getBindingRangeCount();
        for (u32 i = 0; i < rangeCount; ++i) {
          auto range = tl->getBindingRangeType(i);


          switch (range) {
#define MAP(slangType, renType)         \
  case slang::BindingType::slangType: { \
    resourceType = Type::renType;       \
    break;                              \
  }
            MAP(Sampler, Sampler);
            MAP(Texture, Image);  // Not 100% sure about this.
            MAP(ConstantBuffer, UniformBuffer);
            MAP(ParameterBlock, ParameterBlock);
            MAP(TypedBuffer, StorageBuffer);  // ? Maybe uniform?
            MAP(RawBuffer, StorageBuffer);

            MAP(MutableTexture, StorageImage);
            MAP(MutableTypedBuffer, StorageBuffer);
            MAP(MutableRawBuffer, StorageBuffer);

#undef MAP
            default: {
              ren::errln("resource {} has unhandled binding type {}", name, static_cast<int>(range));
              // abort();
              break;
            }
          }
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

  void ShaderReflection::parseFromSlang(ProgramLayout* programLayout) {
    if (!programLayout) {
      std::cerr << "Invalid ProgramLayout pointer" << std::endl;
      return;
    }
    // {
    //   Slang::ComPtr<slang::IBlob> jsonBlob;
    //   programLayout->toJson(jsonBlob.writeRef());

    //   json j = json::parse(
    //       std::string_view((const char*)jsonBlob->getBufferPointer(),
    //       jsonBlob->getBufferSize()));
    //   FILE* f = fopen("slang_program_layout.json", "w");
    //   auto formatted = j.dump(2);
    //   fwrite(formatted.data(), 1, formatted.size(), f);
    //   fclose(f);
    // }

    // Optional: Print debug info (uncomment to see detailed reflection)
    // ReflectingPrinting printer;
    // printer.printProgramLayout(programLayout, SlangCompileTarget::SLANG_SPIRV);

    // Create root node
    auto root = newNode(Type::LogicalGroup, "Root", {/* empty on purpose */});

    // Parse global parameters
    auto globalParams = programLayout->getGlobalParamsVarLayout();
    if (globalParams) {
      auto globalTypeLayout = globalParams->getTypeLayout();
      if (globalTypeLayout) {
        if (globalTypeLayout->getKind() != slang::TypeReflection::Kind::Struct) {
          // throw std::runtime_error(
          //     "Slang globals must be explicit, and without automatically inserted constant structs or parameter blocks. This is a temporary
          //     limitation.");
        }
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
        return Type::Unknown;
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
        binding.type = node->type;
        binding.path = currentPath;
        if (node->location.arrayIndex) {
          binding.count = std::max(binding.count, *node->location.arrayIndex + 1);
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


  static ImGuiTreeNodeFlags tree_node_flags_base =
      ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_DrawLinesFull;

  static void inspectNodeRecursive(const ShaderReflection::Node* node, int depth) {
    if (!node) {
      return;
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    const bool isLeaf = node->members.empty();

    ImGuiTreeNodeFlags node_flags = tree_node_flags_base;
    if (isLeaf) {
      node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }


    char name_buf[256];

    if (node->name.length() == 0) {
      if (node->location.arrayIndex) {
        if (node->location.arrayIndex > 0) {
          node_flags &= ~ImGuiTreeNodeFlags_DefaultOpen;
        }
        snprintf(name_buf, sizeof(name_buf), "[#%d]", *node->location.arrayIndex);
      } else {
        snprintf(name_buf, sizeof(name_buf), "<unnamed>");
      }
    } else {
      snprintf(name_buf, sizeof(name_buf), "%s", node->name.c_str());
    }

    bool open = ImGui::TreeNodeEx((void*)node, node_flags, "%s", name_buf);

    // Type column
    ImGui::TableNextColumn();
    ImGui::Text("%s", node->type.toString().c_str());


    ImGui::TableNextColumn();
    ImGui::Text("%s", node->location.pushConstant ? "Yes" : "");

#define SHOW_LOC(FIELD)                       \
  ImGui::TableNextColumn();                   \
  if (node->location.FIELD) {                 \
    ImGui::Text("%d", *node->location.FIELD); \
  }

    // Set column
    SHOW_LOC(bindingSet);
    SHOW_LOC(bindingIndex);
    SHOW_LOC(byteOffset);
    SHOW_LOC(byteSize);
    SHOW_LOC(arrayIndex);
    SHOW_LOC(varyingIn);
    SHOW_LOC(varyingOut);



    // Recurse into members
    if (open && !isLeaf) {
      for (const auto* member : node->members) {
        inspectNodeRecursive(member, depth + 1);
      }
      ImGui::TreePop();
    }
  }

  void ShaderReflection::inspect() {
    if (!root) {
      ImGui::Text("No reflection data available");
      return;
    }

    const float TEXT_BASE_WIDTH = ImGui::CalcTextSize("A").x;

    static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg;

    float locWidth = TEXT_BASE_WIDTH * 3.5f;
    ImGuiTableColumnFlags locFlags = ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoResize;

    if (ImGui::BeginTable("##ShaderReflection", 10, flags)) {
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, TEXT_BASE_WIDTH * 25.0f);

      ImGui::TableSetupColumn("PC?", locFlags, locWidth);
      ImGui::TableSetupColumn("SET", locFlags, locWidth);
      ImGui::TableSetupColumn("IND", locFlags, locWidth);
      ImGui::TableSetupColumn("OFF", locFlags, locWidth);
      ImGui::TableSetupColumn("SIZ", locFlags, locWidth);
      ImGui::TableSetupColumn("AID", locFlags, locWidth);
      ImGui::TableSetupColumn("VIN", locFlags, locWidth);
      ImGui::TableSetupColumn("VOUT", locFlags, locWidth);

      ImGui::TableHeadersRow();


      // Draw root members
      for (const auto* member : root->members) {
        inspectNodeRecursive(member, 0);
      }

      ImGui::EndTable();
    }


#if 0
    ImGui::Separator();
    ImGui::Text("Reflected Bindings (not instantiated):");

    if (ImGui::BeginTable("##ShaderBindings", 3, flags)) {
      ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Descriptor Set");
      ImGui::TableSetupColumn("Index");
      ImGui::TableHeadersRow();

      for (const auto& binding : bindings) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", binding.path.c_str());

        ImGui::TableNextColumn();
        ImGui::Text("%d", binding.set);

        ImGui::TableNextColumn();
        ImGui::Text("%d", binding.index);
      }

      ImGui::EndTable();
    }
#endif
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
