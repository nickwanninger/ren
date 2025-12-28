#include "./ShaderReflection.h"
#include <unordered_map>
#include <string>
#include <set>

#include "./SlangPrinter.h"
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
  case Type::a: return #a;
#include "./ShaderReflectionTypes.def"
#undef TYPE
      case Type::Unknown: return "Unknown";
    }
  }

  json ShaderReflection::Location::toJson() const {
    json result;
    // result["depth"] = depth;
    if (pushConstant) { result["pushConstant"] = pushConstant; }
    if (bindingSet) { result["bindingSet"] = *bindingSet; }
    if (bindingIndex) { result["bindingIndex"] = *bindingIndex; }
    if (byteOffset) { result["byteOffset"] = *byteOffset; }
    if (byteSize) { result["byteSize"] = *byteSize; }
    if (arrayIndex) { result["arrayIndex"] = *arrayIndex; }
    if (varyingIn) { result["varyingIn"] = *varyingIn; }
    if (varyingOut) { result["varyingOut"] = *varyingOut; }
    return result;
  }


  std::string BindingType::toString() const {
    if (!elementType.has_value()) { return bindingTypeToString(type); }
    return fmt::format("{}<{}>", bindingTypeToString(type), bindingTypeToString(*elementType));
  }

  json Node::toJson() const {
    json result;

    result["name"] = name;
    result["type"] = type.toString();  // bindingTypeToString(type);

    if (!meta.is_null()) { result["meta"] = meta; }

    // if (location.bindingIndex && location.bindingSet) {
    //   result["binding"] = fmt::format("{}.{}", *location.bindingSet, *location.bindingIndex);
    // }

    // if (location.byteSize) { result["size"] = *location.byteSize; }

    auto locationJson = location.toJson();
    if (!locationJson.is_null()) { result["location"] = locationJson; }

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
      case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return Type::UniformBuffer;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return Type::StorageBuffer;
      case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return Type::Texture;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return Type::Image;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return Type::Sampler;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: return Type::StorageImage;
      default: return Type::Unknown;
    }
  }

  // Recursively parse members of a block variable (struct, array, etc.)
  void ShaderReflection::parseBlockVariableMembers(const SpvReflectBlockVariable* var,
                                                   Node* parent_node, Location loc) {
    if (!var || var->member_count == 0) { return; }


    for (uint32_t i = 0; i < var->member_count; ++i) {
      const auto* member = &var->members[i];
      if (!member || !member->name) continue;

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
      else if (member->type_description && member->type_description->op == SpvOpTypeStruct &&
               member->member_count > 0) {
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
        if (!binding || !binding->name) continue;

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
      if (!pc_block || !pc_block->name) continue;

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




  static void applyOffset(std::optional<u32>& base, u32 offset) {
    base = base ? *base + offset : offset;
  }

  bool adjustLocation(const char* debugName, ShaderReflection::Location& loc,
                      slang::ParameterCategory layoutUnit, u32 offset, u32 space) {
    switch (layoutUnit) {
      case slang::ParameterCategory::DescriptorTableSlot:
        applyOffset(loc.bindingIndex, offset);
        applyOffset(loc.bindingSet, space);
        break;
      case slang::ParameterCategory::SubElementRegisterSpace:
        applyOffset(loc.bindingIndex, 0);
        applyOffset(loc.bindingSet, offset);
        break;
      case slang::ParameterCategory::Uniform: applyOffset(loc.byteOffset, offset); break;
      case slang::ParameterCategory::PushConstantBuffer: loc.pushConstant = true; break;
      case slang::ParameterCategory::VaryingInput: applyOffset(loc.varyingIn, offset); break;
      case slang::ParameterCategory::VaryingOutput: applyOffset(loc.varyingOut, offset); break;

      default: {
        ren::warnln("Unhandled unit for variable '{}', layoutUnit={}, offset={}, space={}",
                    debugName, static_cast<int>(layoutUnit), offset, space);
        break;
        // return false;
      }
    };
    return true;
  }

  bool adjustLocation(VariableLayoutReflection* vl, ShaderReflection::Location& loc) {
    if (!vl) return false;

    const char* debugName = vl->getName() ? vl->getName() : "<unnamed>";

    auto newLoc = loc;
    int usedLayoutUnitCount = vl->getCategoryCount();
    for (int i = 0; i < usedLayoutUnitCount; ++i) {
      auto layoutUnit = vl->getCategoryByIndex(i);
      auto offset = vl->getOffset(layoutUnit);
      auto space = vl->getBindingSpace(layoutUnit);

      if (!adjustLocation(debugName, newLoc, layoutUnit, offset, space)) { return false; };
    }

    // Update location
    loc = newLoc;

    return true;
  }



  ShaderReflection::Type mapSlangType(SlangResourceShape shape, SlangResourceAccess access) {
    using Type = ShaderReflection::Type;
    switch (shape) {
      case SlangResourceShape::SLANG_STRUCTURED_BUFFER:
      case SlangResourceShape::SLANG_BYTE_ADDRESS_BUFFER: return Type::StorageBuffer;
      default: return Type::Unknown;
    }
  }

  Node* ShaderReflection::extractVariableLayout(VariableLayoutReflection* vl,
                                                TypeLayoutReflection* tl, Location loc) {
    // if (!vl) return nullptr;
    // if (!tl) return nullptr;


    if (vl && tl) {
      if (adjustLocation(vl, loc) == false) { return nullptr; }
    }



    const char* name = "";

    if (vl && vl->getName()) name = vl->getName();


    size_t uniformSize = tl->getSize(slang::ParameterCategory::Uniform);
    loc.byteSize = uniformSize;

    using Kind = slang::TypeReflection::Kind;
    auto kind = tl->getKind();

    ren::println("name={} kind={} size={}", name, static_cast<int>(kind), uniformSize);
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
        ren::println("Array!");
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
              ren::warnln("Array element kind not being handled correctly yet! elementKind={}",
                          static_cast<int>(elementKind));
              elementType = Type::Unknown;
              break;
            }
          }
        }

        BindingType t = elementType == Type::Unknown ? BindingType(nodeType)
                                                     : BindingType(nodeType, elementType);
        auto* node = newNode(t, name, loc);


        // go over element?


        u32 stride = tl->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
        ren::println(
            "Array detected: name='{}', elementCount={}, elementType={}, evtl:{},{}, stride={}",
            name, elementCount, static_cast<int>(elementType), (void*)evl, (void*)etl, stride);




        for (u32 i = 0; i < elementCount; ++i) {
          auto l = loc.child();
          l.arrayIndex = i;
          if (stride > 0) { applyOffset(l.byteOffset, i * stride); }
          auto n = extractVariableLayout(evl, etl, l);
          ren::println("Array element {} extracted: {}", i, (void*)n);
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
            case slang::BindingType::Texture: {
              resourceType = Type::Texture;
              break;
            }
            case slang::BindingType::Sampler: {
              resourceType = Type::Sampler;
              break;
            }
            case slang::BindingType::CombinedTextureSampler: {
              resourceType = Type::Texture;
              break;
            }

            default: {
              break;
            }
          }
        }




        if (shape & SLANG_TEXTURE_1D || shape & SLANG_TEXTURE_2D || shape & SLANG_TEXTURE_3D ||
            shape & SLANG_TEXTURE_CUBE) {
          if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
            resourceType = Type::StorageImage;  // Storage image
          } else {
            resourceType = Type::Texture;  // Sampled texture
          }
          // } else if (shape == slanm) {
          //   resourceType = Type::Sampler;
        } else if (shape & SLANG_STRUCTURED_BUFFER || shape & SLANG_BYTE_ADDRESS_BUFFER) {
          if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
            resourceType = Type::StorageBuffer;
          } else {
            resourceType = Type::UniformBuffer;
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
          throw std::runtime_error(
              "Slang globals must be explicit, and without automatically inserted constant structs or parameter blocks. This is a temporary limitation.");
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
                  ren::warnln("Unsupported top-level global variable '{}', type={}", node->name,
                              node->type.toString());
                }

                root->members.push_back(node);
              }
            }
          }
        }
      }
    }

#if 1
    // Parse entry-point-specific parameters
    u32 entryPointCount = programLayout->getEntryPointCount();
    for (u32 epIndex = 0; epIndex < entryPointCount; ++epIndex) {
      auto entryPoint = programLayout->getEntryPointByIndex(epIndex);
      if (!entryPoint) continue;


      // Parse entry point parameters
      auto entryParams = entryPoint->getVarLayout();
      if (!entryParams) continue;

      auto entryTypeLayout = entryParams->getTypeLayout();
      if (!entryTypeLayout) continue;


      auto epRoot = newNode(Type::EntryPoint, entryPoint->getName(), {/* empty on purpose */});
      root->members.push_back(epRoot);

      u32 fieldCount = entryTypeLayout->getFieldCount();
      for (u32 i = 0; i < fieldCount; ++i) {
        auto fieldVarLayout = entryTypeLayout->getFieldByIndex(i);
        if (!fieldVarLayout) continue;
        auto fieldTypeLayout = fieldVarLayout->getTypeLayout();
        if (!fieldTypeLayout) continue;
        // Create fresh AccessPath for each entry point parameter
        ShaderReflection::Location loc;
        auto* childNode = extractVariableLayout(fieldVarLayout, fieldTypeLayout, loc);
        if (childNode) { epRoot->members.push_back(childNode); }
      }
    }
#endif


    // Set root node
    this->root = mergeNodes(this->root, root);

    extractBindings();
  }


  static bool hasRealBinding(const BindingType type) {
    switch (type.type) {
#define TYPE(a, flags, ...) \
  case ShaderReflection::Type::a: return (flags & SRT_RESOURCE) ? true : false;
#include "./ShaderReflectionTypes.def"
#undef TYPE
      default: return Type::Unknown;
    }
  }


  void ShaderReflection::extractBindings(void) {
    REN_PROFILE_SCOPE("ShaderReflection::extractBindings");
    bindings.clear();
    if (!root) return;

    std::function<void(Node*, const std::string&)> traverse;
    traverse = [&](Node* node, const std::string& path) {
      REN_PROFILE_SCOPE("traverse");
      if (!node) return;

      std::string currentPath = path.empty() ? node->name : path + "." + node->name;

      // If this node has binding info, add to bindings list
      if (node->location.bindingSet && node->location.bindingIndex && hasRealBinding(node->type)) {
        Binding binding;
        binding.set = *node->location.bindingSet;
        binding.index = *node->location.bindingIndex;
        binding.path = currentPath;
        binding.node = node;
        bindings.push_back(binding);
      }

      // Recurse into members
      for (auto* member : node->members) {
        traverse(member, currentPath);
      }
    };

    for (auto* member : root->members) {
      traverse(member, "");
    }

    // Sort bindings by set, then by index
    std::sort(bindings.begin(), bindings.end(), [](const Binding& a, const Binding& b) {
      if (a.set != b.set) { return a.set < b.set; }
      return a.index < b.index;
    });


    ren::dbgln("Extracted {} bindings:", bindings.size());
    for (auto& binding : bindings) {
      ren::dbgln("- {}.{} = {}", binding.set, binding.index, binding.path);
    }
  }

  // Helper function to check if two Location objects are compatible
  static bool locationsAreCompatible(const ShaderReflection::Location& locA,
                                     const ShaderReflection::Location& locB) {
    return locA.pushConstant == locB.pushConstant && locA.bindingSet == locB.bindingSet &&
           locA.bindingIndex == locB.bindingIndex && locA.byteOffset == locB.byteOffset &&
           locA.byteSize == locB.byteSize && locA.arrayIndex == locB.arrayIndex &&
           locA.varyingIn == locB.varyingIn && locA.varyingOut == locB.varyingOut;
  }

  // Recursive divide-and-conquer merge of two nodes and their subtrees
  Node* ShaderReflection::mergeNodes(Node* nodeA, Node* nodeB) {
    if (!nodeA) return nodeB;
    if (!nodeB) return nodeA;

    // Build a map of members in nodeA by name for O(1) lookup
    std::map<std::string, Node*> membersA;
    for (auto* member : nodeA->members) {
      membersA[member->name] = member;
    }

    // Build a set of member names in nodeB
    std::set<std::string> memberNamesB;
    std::vector<Node*> membersToAdd;

    for (auto* memberB : nodeB->members) {
      memberNamesB.insert(memberB->name);

      auto it = membersA.find(memberB->name);
      if (it != membersA.end()) {
        // Member exists in both trees
        auto* memberA = it->second;

        // Verify type compatibility
        if (memberA->type != memberB->type) {
          throw std::runtime_error(
              fmt::format("Shader merge conflict: '{}' has incompatible types ({} vs {})",
                          memberB->name, memberA->type.toString(), memberB->type.toString()));
        }

        // Verify location compatibility
        if (!locationsAreCompatible(memberA->location, memberB->location)) {
          throw std::runtime_error(fmt::format(
              "Shader merge conflict: '{}' has incompatible location info", memberB->name));
        }

        // If both are container types, recursively merge their members
        if (memberA->type == Type::LogicalGroup || memberA->type == Type::Struct ||
            memberA->type == Type::ParameterBlock) {
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


  static ImGuiTreeNodeFlags tree_node_flags_base = ImGuiTreeNodeFlags_SpanAllColumns |
                                                   ImGuiTreeNodeFlags_DefaultOpen |
                                                   ImGuiTreeNodeFlags_DrawLinesFull;

  static void inspectNodeRecursive(const ShaderReflection::Node* node, int depth) {
    if (!node) return;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    const bool isLeaf = node->members.empty();

    ImGuiTreeNodeFlags node_flags = tree_node_flags_base;
    if (isLeaf) { node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; }


    char name_buf[256];

    if (node->name.length() == 0) {
      if (node->location.arrayIndex) {
        if (node->location.arrayIndex > 0) node_flags &= ~ImGuiTreeNodeFlags_DefaultOpen;
        snprintf(name_buf, sizeof(name_buf), "[#%d]", *node->location.arrayIndex);
      } else {
        snprintf(name_buf, sizeof(name_buf), "<unnamed>");
      }
    } else {
      snprintf(name_buf, sizeof(name_buf), "%s", node->name.c_str());
    }

    bool open = ImGui::TreeNodeEx((void*)node, node_flags, name_buf);

    // Type column
    ImGui::TableNextColumn();
    ImGui::Text("%s", node->type.toString().c_str());


    ImGui::TableNextColumn();
    ImGui::Text("%s", node->location.pushConstant ? "Yes" : "");

#define SHOW_LOC(FIELD)     \
  ImGui::TableNextColumn(); \
  if (node->location.FIELD) { ImGui::Text("%d", *node->location.FIELD); }

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

    static ImGuiTableFlags flags =
        ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_RowBg;

    float locWidth = TEXT_BASE_WIDTH * 3.5f;
    ImGuiTableColumnFlags locFlags = ImGuiTableColumnFlags_WidthFixed |
                                     ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoResize;

    if (ImGui::BeginTable("##ShaderReflection", 10, flags)) {
      ImGui::TableSetupColumn("Name",
                              ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
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

}  // namespace ren
