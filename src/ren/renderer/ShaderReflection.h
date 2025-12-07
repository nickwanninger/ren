#pragma once

#include <slang/slang.h>
#include <ren/types.h>
#include <spirv_reflect/spirv_reflect.h>

// This file implements an abstraction over various shader resource binding
// reflection systems such as spirv-reflect and slang reflection. It allows
// querying information about shader resources (uniform buffers, textures,
// fields, array index, etc.) by name or index and exposes them as a concrete
// datastructure in the engine itself.
// In essence, this structure represents the reflection information as a sort of "AST"

namespace ren {




  class ShaderReflection {
   public:
    ShaderReflection() = default;
    ~ShaderReflection() = default;


    void parseFromSpirv(const u8* spirvData, size_t spirvSize);
    void parseFromSlang(slang::ProgramLayout* programLayout);




    // These are the types of bindings we can have in a shader.
    enum BindingType : u8 {
      // Uniform buffer / constant buffer
      // - Has Binding info, might have members
      UniformBuffer,
      // Storage buffer / read-write buffer
      // - Has Binding info, might have members
      StorageBuffer,
      // CombinedImageSampler (COMBINED_IMAGE_SAMPLER in Vulkan)
      // - Has binding info, no members
      CombinedImageSampler,
      // Sampler (separate sampler in Vulkan)
      // - Has binding info, no members
      Sampler,
      // Image (storage image in Vulkan)
      // - Has binding info, no members
      Image,
      // StorageImage (storage image in Vulkan)
      // - Has binding info, no members
      StorageImage,
      // Push constant block
      // - Has members, no binding info, but is special cased.
      PushConstant,
      // Parameter block (Slang concept, maps to a descriptor set)
      // - Has descriptor set index, has members, represents a ParameterBlock<T>
      ParameterBlock,

      // LogicalGroup
      // Just a logical grouping of other nodes, no binding info itself
      LogicalGroup,


      // an Entrypoint in the shader
      EntryPoint,


      // Struct (block of other nodes)
      // - Has members, does not have binding info itself
      Struct,
      // Array (todo:)
      // This node represents something like:
      // sampler2D myTextures[4];
      // Not sure how to represent array size yet.
      Array,

      // A field in a uniform buffer or push constant
      // - Has binding info if the parent is a uniform buffer, otherwiise not.
      Field,  // Field type in a uniform (if we have this info)

      // Unknown / unrecognized type (error, should not happen)
      Unknown
    };

    struct Location {
      u8 depth = 0;               // depth in the tree.
      bool pushConstant = false;  // Is this location within a push constant block

      std::optional<u32> bindingSet;    // Descriptor set
      std::optional<u32> bindingIndex;  // Binding index within the set
      std::optional<u32> byteOffset;    // Byte offset into a buffer (fields)
      std::optional<u32> byteSize;      // Size in bytes (if applicable)
      std::optional<u32> arrayIndex;    // Array index (if applicable)

      std::optional<u32> varyingIn, varyingOut;  // Varying location (if applicable)

      // Make a child of this location
      Location child(void) {
        Location loc = *this;
        loc.depth += 1;
        return loc;
      }

      json toJson() const;
    };
    static constexpr size_t LocationSize = sizeof(Location);


    // A reflection node represents an element in the tree of reflection.  For
    // example, a struct type would be a node with child nodes for each field, but
    // which doesn't have a binding set/index.
    // This interface is implemented in the ShaderReflection.cpp file as various
    // node types with the above BindingTypes
    struct Node {
     public:
      std::string name;
      Location location;
      BindingType type;

      std::vector<Node*> members;

      json meta;  // Additional metadata (if any)

      json toJson(void) const;
    };


    struct Binding {
      u32 set, index;
      std::string path;  // "material.albedo", for example
      Node* node;
    };



    Node* getRoot() const { return root; }

    std::vector<Binding> bindings;

   private:
    // Recursive divide-and-conquer merge of two nodes and their subtrees
    // Verifies type and location compatibility, asserts on conflicts
    Node* mergeNodes(Node* nodeA, Node* nodeB);
    // Spirv Extraction

    void parseBlockVariableMembers(const SpvReflectBlockVariable* var, Node* parent_node,
                                   Location loc);

    // methods to extract from slang reflection information.
    Node* extractVariableLayout(slang::VariableLayoutReflection* varLayout,
                                slang::TypeLayoutReflection* typeLayout, Location parentLocation);



    Node* newNode(BindingType type, const char* name, const Location& loc) {
      auto node = makeBox<Node>();
      node->type = type;
      node->name = name;
      node->location = loc;
      Node* nodePtr = node.get();
      allNodes.push_back(std::move(node));
      return nodePtr;
    }


    void extractBindings(void);

   private:
    // This root node of the tree should be a struct to unify all the interfaces.
    Node* root = nullptr;
    // The nodes of the reflection tree are owned by this object.
    // We use raw pointers in the tree itself for simplicity.
    std::vector<box<Node>> allNodes;
  };
}  // namespace ren