#pragma once

#include <slang.h>
#include <ren/types.h>
#include <ren/core/Option.h>
#include <ren/core/OptionalInt.h>

namespace ren {

  // Owns the push constant fields and material schema of one linked Slang program.
  // Descriptor layouts follow the fixed shader ABI and stay outside this model.
  class ShaderReflection {
   public:
    enum class ValueKind : u8 {
      Unknown,
      Scalar,
      Vector,
      Matrix,
      Struct,
      Array,
      Pointer,
      Enum,
    };

    enum class ScalarKind : u8 {
      None,
      Bool,
      Int8,
      UInt8,
      Int16,
      UInt16,
      Int32,
      UInt32,
      Int64,
      UInt64,
      Float16,
      Float32,
      Float64,
    };

    struct MaterialAttribute {
      std::string name;
      std::vector<json> arguments;

      json toJson() const;
    };

    struct MaterialField {
      std::string name;
      std::string typeName;
      ValueKind kind = ValueKind::Unknown;
      ScalarKind scalarKind = ScalarKind::None;
      // Relative to the enclosing material/struct/array element.
      u32 byteOffset = 0;
      u32 byteSize = 0;
      u32 alignment = 0;
      u32 rowCount = 0;
      u32 columnCount = 0;
      u32 elementCount = 0;
      u32 elementStride = 0;
      std::vector<MaterialAttribute> attributes;
      std::vector<MaterialField> fields;

      json toJson() const;
    };

    struct MaterialSchema {
      std::string pushConstantName;
      u32 pushConstantByteSize = 0;
      u32 drawPointerOffset = 0;
      u32 objectPointerOffset = 0;
      u32 materialPointerOffset = 0;
      std::string typeName;
      u32 byteSize = 0;
      u32 alignment = 0;
      std::vector<MaterialField> fields;

      json toJson() const;
      void inspect() const;
    };

    enum class Type : u8 { Root, PushConstant, Struct, Array, Scalar, Pointer };

    // Stores an absolute byte location within a push constant block.
    struct Location {
      bool pushConstant = false;
      OptionalInt<u32> byteOffset;
      OptionalInt<u32> byteSize;

      // Returns the byte location for diagnostic output.
      json toJson() const;
    };

    // Describes one push constant field. Pointers remain leaves; the material
    // schema separately describes the structure behind MaterialRoot.materials.
    struct Node {
      std::string name;
      Location location;
      Type type = Type::Struct;
      std::vector<Node*> members;

      // Returns the field kind for diagnostic output.
      const char* getTypeName() const;
      // Returns this field and its children for diagnostic output.
      json toJson() const;
    };

    // Returns the root group. Parsing must precede lookup.
    const Node* getRoot() const { return root; }
    // Returns the schema of the MaterialDrawParams push constant, if present.
    const Option<MaterialSchema>& getMaterialSchema() const { return materialSchema; }
    // Replaces reflection with the push constants and material schema of a program.
    void parseFromSlang(slang::ProgramLayout* programLayout);
    // Draws the push constant fields and material schema.
    void inspect();
    // Returns the push constant fields and material schema for diagnostic output.
    json toJson() const;

   private:
    Node* newNode(Type type, const char* name, const Location& location);
    Node* extractValue(slang::VariableLayoutReflection* variable, slang::TypeLayoutReflection* layout, Location location);

    Node* root = nullptr;
    std::vector<Box<Node>> allNodes;
    Option<MaterialSchema> materialSchema = None;
  };
}  // namespace ren
