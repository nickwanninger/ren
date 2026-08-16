#pragma once

#include <slang.h>
#include <ren/types.h>
#include <spirv_reflect/spirv_reflect.h>
#include <ren/core/OptionalInt.h>

// This file implements an abstraction over various shader resource binding
// reflection systems such as spirv-reflect and slang reflection. It allows
// querying information about shader resources (uniform buffers, textures,
// fields, array index, etc.) by name or index and exposes them as a concrete
// datastructure in the engine itself.
// In essence, this structure represents the reflection information as a sort of "AST"

namespace ren {


// SRT = Shader Reflection Type.  These flags are used to describe various
// bitmasks for each shader reflection type from ShaderReflectionTypes.def
#define SRT_RESOURCE \
  (1LU << 0)                   // This is a resource (requires binding). A semantic of this is that elements are
                               // *not* resources.
#define SRT_BUFFER (1LU << 1)  // This is a buffer type (UBO/SSBO). We include PCs in here.
#define SRT_WRITE (1LU << 2)   // This type supports write access.




  class ShaderReflection {
   public:
    // These are the types of bindings we can have in a shader.
    enum Type : u8 {
#define TYPE(a, ...) a,
#include "./ShaderReflectionTypes.def"
#undef TYPE
      Unknown,
    };

    // A class that lets us represent the type of a binding in a higher level way.
    struct BindingType {
      Type type = Type::Unknown;
      // For arrays, the element type
      std::optional<Type> elementType;

      inline BindingType(Type type)
          : type(type)
          , elementType(std::nullopt) {}
      inline BindingType(Type type, Type elementType)
          : type(type)
          , elementType(elementType) {}

      std::string toString(void) const;
      static bool allowedInTopLevel(Type type);
      static VkDescriptorType toVkDescriptorType(Type type);
      // Resolves ResourceArray through its element type; use this over the
      // static overload whenever a full BindingType is available.
      VkDescriptorType toVkDescriptorType() const;

      inline bool operator==(const BindingType& other) const { return type == other.type && elementType == other.elementType; }
      inline bool operator!=(const BindingType& other) const { return !(*this == other); }
    };

    struct Location {
      SlangStage stage;
      bool pushConstant = false;  // Is this location within a push constant block
      OptionalInt<u16> bindingSet;             // Descriptor set
      OptionalInt<u16> bindingIndex;           // Binding index within the set
      OptionalInt<u16> varyingIn, varyingOut;  // Varying location (if applicable)
      OptionalInt<u32> byteOffset;             // Byte offset into a buffer (scalars)
      OptionalInt<u32> byteSize;               // Size in bytes (if applicable)
      OptionalInt<u32> arrayIndex;             // Array index (if applicable)

      Location child(void) const;
      json toJson() const;
      bool adjustLocation(slang::VariableLayoutReflection* vl);
    };
    static constexpr size_t LocationSize = sizeof(Location);


    struct Node {
     public:
      std::string name;
      Location location;
      BindingType type{Type::Unknown};
      std::vector<Node*> members;
      json meta;  // Additional metadata (if any)

      json toJson(void) const;

    };


    struct Binding {
      u32 set, index, count = 1;
      std::string path;  // "material.albedo", for example
      BindingType type = Type::Unknown;
      Node* node;

      TO_JSON(Binding, set, index, count, path /* TODO: type */);
    };


    ShaderReflection() = default;
    ~ShaderReflection() = default;



    Node* getRoot() const { return root; }
    void parseFromSpirv(const u8* spirvData, size_t spirvSize);
    void parseFromSlang(slang::ProgramLayout* programLayout, bool dumpDebugInfo = false);
    void inspect();
    json toJson() const;

    std::vector<Binding> bindings;

   private:
    Node* newNode(BindingType type, const char* name, const Location& loc);
    Node* mergeNodes(Node* nodeA, Node* nodeB);
    void parseBlockVariableMembers(const SpvReflectBlockVariable* var, Node* parent_node, Location loc);
    Node* extractVariableLayout(slang::VariableLayoutReflection* varLayout, slang::TypeLayoutReflection* typeLayout, Location parentLocation);
    void extractBindings(void);

   private:
    // This root node of the tree should be a struct to unify all the interfaces.
    Node* root = nullptr;
    // The nodes of the reflection tree are owned by this object.
    // We use raw pointers in the tree itself for simplicity.
    std::vector<Box<Node>> allNodes;
  };
}  // namespace ren
