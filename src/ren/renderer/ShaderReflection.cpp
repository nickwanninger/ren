#include "./ShaderReflection.h"
#include <unordered_map>
#include <string>
#include <set>

#include "./SlangPrinter.h"
#include <slang/slang-com-ptr.h>
#include <slang/slang.h>

namespace ren {




  // ============================================================================
  // Node Class Implementations
  // ============================================================================




  // Type alias for convenience within this namespace
  using BindingType = ShaderReflection::BindingType;
  using Node = ShaderReflection::Node;

  const char* bindingTypeToString(BindingType type) {
    switch (type) {
      case BindingType::UniformBuffer: return "UniformBuffer";
      case BindingType::StorageBuffer: return "StorageBuffer";
      case BindingType::CombinedImageSampler: return "CombinedImageSampler";
      case BindingType::Sampler: return "Sampler";
      case BindingType::Image: return "Image";
      case BindingType::StorageImage: return "StorageImage";
      case BindingType::PushConstant: return "PushConstant";
      case BindingType::ParameterBlock: return "ParameterBlock";
      case BindingType::LogicalGroup: return "LogicalGroup";
      case BindingType::Struct: return "Struct";
      case BindingType::Array: return "Array";
      case BindingType::Field: return "Field";
      case BindingType::EntryPoint: return "EntryPoint";
      case BindingType::Unknown: return "Unknown";
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

  json Node::toJson() const {
    json result;

    result["name"] = name;
    result["type"] = bindingTypeToString(type);

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

  static BindingType mapDescriptorType(SpvReflectDescriptorType type) {
    switch (type) {
      case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return BindingType::UniformBuffer;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return BindingType::StorageBuffer;
      case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        return BindingType::CombinedImageSampler;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return BindingType::Image;
      case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER: return BindingType::Sampler;
      case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE: return BindingType::StorageImage;
      default: return BindingType::Unknown;
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
        // BindingType element_type = BindingType::Unknown;
        // if (member->type_description) {
        //   element_type =
        //       mapDescriptorType(SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER);  // Default
        //       placeholder
        // }

        auto* n = newNode(BindingType::Array, member->name, memberLoc);
        parent_node->members.push_back(n);
      }
      // Check if this member is a struct type
      else if (member->type_description && member->type_description->op == SpvOpTypeStruct &&
               member->member_count > 0) {
        // Create struct node with offset/size info
        auto* n = newNode(BindingType::Struct, member->name, memberLoc);
        parseBlockVariableMembers(member, n, memberLoc.child());
        parent_node->members.push_back(n);
      }
      // Regular field (scalar, vector, matrix, etc.)
      else {
        auto* n = newNode(BindingType::Field, member->name, memberLoc);
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



    auto root = newNode(BindingType::LogicalGroup, "Root", {/* empty on purpose */});

    // Parse descriptor bindings
    uint32_t binding_count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &binding_count, nullptr);

    if (binding_count > 0) {
      std::vector<SpvReflectDescriptorBinding*> bindings(binding_count);
      spvReflectEnumerateDescriptorBindings(&module, &binding_count, bindings.data());

      for (const auto* binding : bindings) {
        if (!binding || !binding->name) continue;

        BindingType type = mapDescriptorType(binding->descriptor_type);


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
          structNode =
              newNode(BindingType::LogicalGroup, structName.c_str(), {/* empty on purpose */});
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


  static void printIndent(u32 depth) {
    return;
    for (u32 i = 0; i < depth; ++i) {
      fmt::print("  ");
    }
  }

  static void printLocation(const char* name, const ShaderReflection::Location& loc) {
    return;
    printIndent(loc.depth);
    fmt::print("Location for {}: ", name);

    fmt::print("depth={} ", loc.depth);

    if (loc.bindingSet) fmt::print("set={} ", *loc.bindingSet);
    if (loc.bindingIndex) fmt::print("binding={} ", *loc.bindingIndex);
    if (loc.byteOffset) fmt::print("byte={} ", *loc.byteOffset);
    if (loc.byteSize) fmt::print("size={} ", *loc.byteSize);
    if (loc.arrayIndex) fmt::print("arrayIndex={} ", *loc.arrayIndex);
    if (loc.pushConstant) fmt::print("pushConstant ");

    fmt::print("\n");
  }


  bool adjustLocation(const char* debugName, ShaderReflection::Location& loc,
                      slang::ParameterCategory layoutUnit, u32 offset, u32 space) {
    auto applyOffset = [](std::optional<u32>& base, u32 offset) {
      base = base ? *base + offset : offset;
    };
    // fmt::println("Adjust for variable '{}', layoutUnit={}, offset={}, space={}", debugName,
    //              static_cast<int>(layoutUnit), offset, space);

    switch (layoutUnit) {
      case slang::ParameterCategory::DescriptorTableSlot: {
        applyOffset(loc.bindingIndex, offset);
        applyOffset(loc.bindingSet, space);
        break;
      }

      case slang::ParameterCategory::SubElementRegisterSpace: {
        applyOffset(loc.bindingIndex, 0);
        applyOffset(loc.bindingSet, offset);
        break;
      }

      case slang::ParameterCategory::Uniform: {
        applyOffset(loc.byteOffset, offset);
        break;
      }

      case slang::ParameterCategory::PushConstantBuffer: {
        loc.pushConstant = true;
        break;
      }


      case slang::ParameterCategory::VaryingInput: {
        applyOffset(loc.varyingIn, offset);
        break;
      }

      case slang::ParameterCategory::VaryingOutput: {
        applyOffset(loc.varyingOut, offset);
        break;
      }


      default: {
        fmt::print("\e[33mWarning:\e[0m ");
        fmt::println("Unhandled unit for variable '{}', layoutUnit={}, offset={}, space={}",
                     debugName, static_cast<int>(layoutUnit), offset, space);
        break;
        // return false;
      }
    };
    return true;
  }

  bool adjustLocation(VariableLayoutReflection* vl, ShaderReflection::Location& loc) {
    if (!vl) return false;

    TypeLayoutReflection* tl = vl->getTypeLayout();

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



  BindingType mapSlangType(SlangResourceShape shape, SlangResourceAccess access) {
    //
    return BindingType::Unknown;
  }

  Node* ShaderReflection::extractVariableLayout(VariableLayoutReflection* vl,
                                                TypeLayoutReflection* tl, Location loc) {
    if (!vl) return nullptr;
    if (!tl) return nullptr;
    // fmt::print("\n");

    if (adjustLocation(vl, loc) == false) { return nullptr; }



    auto name = vl->getName() ? vl->getName() : "<unnamed>";


    size_t uniformSize = tl->getSize(slang::ParameterCategory::Uniform);
    loc.byteSize = uniformSize;

    using Kind = slang::TypeReflection::Kind;
    auto kind = tl->getKind();
    switch (kind) {
      case Kind::ParameterBlock: {
        // printLocation(name, loc);
        // printIndent(loc.depth);
        // fmt::println("ParameterBlock {}", name);


        auto evl = tl->getElementVarLayout();
        auto etl = tl->getElementTypeLayout();

        // TODO: we need to handle the case where the parameter block is more than just a collection
        // of bindings (that is, it includes fields.)
        auto* node = extractVariableLayout(evl, etl, loc.child());
        node->name = name;
        node->type = BindingType::LogicalGroup;

        return node;
      }

      case Kind::ConstantBuffer: {
        // printLocation(name, loc);
        // printIndent(loc.depth);
        // fmt::println("ConstantBuffer {} {}", name, uniformSize);

        auto evl = tl->getElementVarLayout();
        auto etl = tl->getElementTypeLayout();

        auto* node = extractVariableLayout(evl, etl, loc.child());


        node->name = name;

        if (loc.pushConstant) {
          node->type = PushConstant;
        } else {
          BindingType bindingType = BindingType::UniformBuffer;
          node->type = bindingType;
        }

        return node;
      }

      case Kind::Array: {
        auto elementTypeLayout = tl->getElementTypeLayout();
        auto elementCount = tl->getElementCount();

        // printLocation(name, loc);
        // printIndent(loc.depth);
        // fmt::println("array: {}", name);


        break;
      }
      case Kind::Matrix:
      case Kind::Vector:
      case Kind::Scalar: {
        auto sizeInBytes = tl->getSize();
        loc.byteSize = sizeInBytes;
        // printLocation(name, loc);
        // printIndent(loc.depth);
        // fmt::println("field: {} {}", name, sizeInBytes);
        auto* fieldNode = newNode(Field, name, loc);
        return fieldNode;
        break;
      }

      case Kind::Struct: {
        // printLocation(name, loc);
        // printIndent(loc.depth);
        // fmt::println("struct: {} {}", name, uniformSize);

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
        // printLocation(name, loc);
        // printIndent(loc.depth);
        // fmt::println("sampler: {} ", name);

        auto* samplerNode = newNode(Sampler, name, loc);
        return samplerNode;
      }

      case Kind::Resource: {
        auto type = tl->getType();
        auto shape = type->getResourceShape();
        auto access = type->getResourceAccess();
        BindingType resourceType = mapSlangType(shape, access);


        auto rangeCount = tl->getBindingRangeCount();
        for (u32 i = 0; i < rangeCount; ++i) {
          auto range = tl->getBindingRangeType(i);

          switch (range) {
            case slang::BindingType::Texture: {
              resourceType = BindingType::CombinedImageSampler;
              break;
            }
            case slang::BindingType::Sampler: {
              resourceType = BindingType::Sampler;
              break;
            }
            case slang::BindingType::CombinedTextureSampler: {
              resourceType = BindingType::CombinedImageSampler;
              break;
            }
            default: {
              break;
            }
          }


          fmt::println("  Binding {} range for {} : {}", i, name, static_cast<int>(range));
        }




        // if (shape & SLANG_TEXTURE_1D || shape & SLANG_TEXTURE_2D || shape & SLANG_TEXTURE_3D ||
        //     shape & SLANG_TEXTURE_CUBE) {
        //   if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
        //     resourceType = BindingType::StorageImage;  // Storage image
        //   } else {
        //     resourceType = BindingType::CombinedImageSampler;  // Sampled texture
        //   }
        //   // } else if (shape == slanm) {
        //   //   resourceType = BindingType::Sampler;
        // } else if (shape & SLANG_STRUCTURED_BUFFER || shape & SLANG_BYTE_ADDRESS_BUFFER) {
        //   if (access == SLANG_RESOURCE_ACCESS_READ_WRITE) {
        //     resourceType = BindingType::StorageBuffer;
        //   } else {
        //     resourceType = BindingType::UniformBuffer;
        //   }
        // }


        // printLocation(name, loc);
        // printIndent(loc.depth);
        // fmt::println("resource: {} ", name);

        auto* resourceNode = newNode(resourceType, name, loc);
        return resourceNode;
      }

      default: {
        printIndent(loc.depth);
        fmt::println("Unhandled kind: {}", static_cast<int>(kind));
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
    auto root = newNode(BindingType::LogicalGroup, "Root", {/* empty on purpose */});

    // Parse global parameters
    auto globalParams = programLayout->getGlobalParamsVarLayout();
    if (globalParams) {
      // auto node = extracVariableLayout(globalParams, Location{});

      auto globalTypeLayout = globalParams->getTypeLayout();
      if (globalTypeLayout) {
        // Traverse global scope (usually a struct containing all globals)
        u32 fieldCount = globalTypeLayout->getFieldCount();
        for (u32 i = 0; i < fieldCount; ++i) {
          auto fieldVarLayout = globalTypeLayout->getFieldByIndex(i);
          if (fieldVarLayout) {
            auto fieldTypeLayout = fieldVarLayout->getTypeLayout();
            if (fieldTypeLayout) {
              // Create a fresh
              ShaderReflection::Location loc;

              auto* node = extractVariableLayout(fieldVarLayout, fieldTypeLayout, loc);
              if (node) root->members.push_back(node);
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


      auto epRoot =
          newNode(BindingType::EntryPoint, entryPoint->getName(), {/* empty on purpose */});
      root->members.push_back(epRoot);
      fmt::println("Entry Point: {}", entryPoint->getName());

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


  static bool hasRealBinding(const ShaderReflection::BindingType type) {
    switch (type) {
      case ShaderReflection::BindingType::UniformBuffer:
      case ShaderReflection::BindingType::StorageBuffer:
      case ShaderReflection::BindingType::CombinedImageSampler:
      case ShaderReflection::BindingType::Sampler:
      case ShaderReflection::BindingType::Image:
      case ShaderReflection::BindingType::StorageImage: {
        return true;
      }
      default: {
        return false;
      }
    }
  }


  void ShaderReflection::extractBindings(void) {
    bindings.clear();
    if (!root) return;

    std::function<void(Node*, const std::string&)> traverse;
    traverse = [&](Node* node, const std::string& path) {
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

    for (auto& binding : bindings) {
      fmt::println("- {}.{} = {}", binding.set, binding.index, binding.path);
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
          throw std::runtime_error(fmt::format("Shader merge conflict: '{}' has incompatible types ({} vs {})",
                                           memberB->name, bindingTypeToString(memberA->type),
                                           bindingTypeToString(memberB->type)));
        }

        // Verify location compatibility
        if (!locationsAreCompatible(memberA->location, memberB->location)) {
          throw std::runtime_error(fmt::format("Shader merge conflict: '{}' has incompatible location info", memberB->name));
        }

        // If both are container types, recursively merge their members
        if (memberA->type == BindingType::LogicalGroup || memberA->type == BindingType::Struct ||
            memberA->type == BindingType::ParameterBlock) {
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

}  // namespace ren