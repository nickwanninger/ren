#pragma once

#include <slang/slang.h>
#include <ren/types.h>

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
      // Texture (COMBINED_IMAGE_SAMPLER in Vulkan)
      // - Has binding info, no members
      Texture,
      // Sampler (separate sampler in Vulkan)
      // - Has binding info, no members
      Sampler,
      // Image (storage image in Vulkan)
      // - Has binding info, no members
      Image,
      // Push constant block
      // - Has members, no binding info, but is special cased.
      PushConstant,
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


    // A reflection node represents an element in the tree of reflection.  For
    // example, a struct type would be a node with child nodes for each field, but
    // which doesn't have a binding set/index.
    // This interface is implemented in the ShaderReflection.cpp file as various
    // node types with the above BindingTypes
    class INode {
     public:
      virtual ~INode() = default;


      // The name of the node.
      virtual std::string_view name() const = 0;


      // Get the size of this node in bytes, if applicable.
      virtual std::optional<u32> size() const { return std::nullopt; }
      // Get the binding type of this node.
      virtual BindingType bindingType() const = 0;
      // Get the binding set/index of this node, if applicable.
      virtual std::optional<std::pair<u32, u32>> binding() const { return std::nullopt; }
      // Get a child node by name, if applicable.
      virtual INode* getMember(const char* name) const { return nullptr; }
      // Get a child node by index, if applicable.
      virtual INode* getMember(u32 index) const { return nullptr; }


      // For introspection, dump this node to JSON format.
      // Roughly follow the schema of
      // - name: string,
      // - type: string, (from BindingType)
      // - binding: { set: int, index: int } // If applicable
      // - size: int, // If applicable
      // - members: [ array of child nodes ] // If applicable
      virtual json toJson(void) const { return {{"type", "Unknown"}, {"name", name()}}; }
    };



    INode *getRoot() const { return root; }


   private:

   // This root node of the tree should be a struct to unify all the interfaces.
    INode* root = nullptr;
    // The nodes of the reflection tree are owned by this object.
    // We use raw pointers in the tree itself for simplicity.
    std::vector<box<INode>> allNodes;
  };
}  // namespace ren