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


// SRT = Shader Reflection Type.  These flags are used to describe various
// bitmasks for each shader reflection type from ShaderReflectionTypes.def
#define SRT_RESOURCE \
  (1LU << 0)  // This is a resource (requires binding). A semantic of this is that elements are
              // *not* resources.
#define SRT_BUFFER (1LU << 1)  // This is a buffer type (UBO/SSBO). We include PCs in here.
#define SRT_WRITE (1LU << 2)   // This type supports write access.




  class ShaderReflection {
   public:
    ShaderReflection() = default;
    ~ShaderReflection() = default;


    void parseFromSpirv(const u8* spirvData, size_t spirvSize);
    void parseFromSlang(slang::ProgramLayout* programLayout);




    // These are the types of bindings we can have in a shader.
    enum Type : u8 {
#define TYPE(a, ...) a,
#include "./ShaderReflectionTypes.def"
#undef TYPE

      // Unknown / unrecognized type (error, should not happen)
      Unknown,
    };



    // A class that lets us represent the type of a binding in a higher level way.
    // For example, a simple uniform buffer would be represented as a Type::UniformBuffer,
    // but a `Sampler2D heap[]` would be represented as a Type::Array with elementType of
    // Texture (combined image sampler). This is basically capturing some simple form of type
    // composition.
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
      inline bool operator==(const BindingType& other) const {
        return type == other.type && elementType == other.elementType;
      }

      inline bool operator!=(const BindingType& other) const { return !(*this == other); }

      static bool allowedInTopLevel(Type type) {
        switch (type) {
#define TYPE(a, flags, allowed) \
  case Type::a: return allowed;
#include "./ShaderReflectionTypes.def"
#undef TYPE
          default: return false;
        }
      }
    };

    struct Location {
      u8 depth = 0;               // depth in the tree.
      bool pushConstant = false;  // Is this location within a push constant block

      std::optional<u32> bindingSet;    // Descriptor set
      std::optional<u32> bindingIndex;  // Binding index within the set
      std::optional<u32> byteOffset;    // Byte offset into a buffer (scalars)
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
      BindingType type{Type::Unknown};

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

    // Inspect the shader reflection in ImGui (tree view with type/location columns)
    void inspect();


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
    std::vector<Box<Node>> allNodes;
  };
}  // namespace ren
