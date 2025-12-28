#include "./ShaderCursor.h"
#include <ren/renderer/Renderer.h>

namespace ren {



  ShaderCursor::ShaderCursor(slang::TypeLayoutReflection* typeLayout)
      : typeLayout(typeLayout) {
    if (!typeLayout) return;
  }

  ShaderCursor ShaderCursor::field(const char* name) {
    // If the cursor is valid, we want to consider the type of data
    // it is referencing.
    //
    switch (typeLayout->getKind()) {
        // The easy/expected case is when the value has a structure type.
        //
      case slang::TypeReflection::Kind::Struct: {
        // We start by looking up the index of a field matching `name`.
        //
        // If there is no such field, we have an error.
        //
        SlangInt fieldIndex = typeLayout->findFieldIndexByName(name);
        if (fieldIndex == -1) break;

        // Once we know the index of the field being referenced,
        // we create a cursor to point at the field, based on
        // the offset information already in this cursor, plus
        // offsets derived from the field's layout.
        //
        slang::VariableLayoutReflection* fieldLayout =
            typeLayout->getFieldByIndex((unsigned int)fieldIndex);
        ShaderCursor fieldCursor = child();

        // The field cursorwill point into the same parent object.
        // fieldCursor.m_baseObject = m_baseObject;

        // The type being pointed to is the tyep of the field.
        //
        fieldCursor.typeLayout = fieldLayout->getTypeLayout();

        // The byte offset is the current offset plus the relative offset of the field.
        // The offset in binding ranges is computed similarly.
        //
        fieldCursor.offset.uniformOffset = offset.uniformOffset + fieldLayout->getOffset();
        fieldCursor.offset.rangeIndex =
            offset.rangeIndex + (uint32_t)typeLayout->getFieldBindingRangeOffset(fieldIndex);

        // The index of the field within any binding ranges will be the same
        // as the index computed for the parent structure.
        //
        // Note: this case would arise for an array of structures with texture-type
        // fields. Suppose we have:
        //
        //      struct S { Texture2D t; Texture2D u; }
        //      S g[4];
        //
        // In this scenario, `g` holds two binding ranges:
        //
        // * Range #0 comprises 4 textures, representing `g[...].t`
        // * Range #1 comprises 4 textures, representing `g[...].u`
        //
        // A cursor for `g[2]` would have a `rangeIndex` of zero but
        // a `rangeArrayIndex` of 2, iindicating that we could end up
        // referencing either range, but no matter what we know the index
        // is 2. Thus when we form a cursor for `g[2].u` we want to
        // apply the binding range offset to get a `rangeIndex` of
        // 1, while the `rangeArrayIndex` is unmodified.
        //
        // The result is that `g[2].u` is stored in range #1 at array index 2.
        //
        fieldCursor.offset.rangeArrayIndex = offset.rangeArrayIndex;

        return fieldCursor;
      }
      // In some cases the user might be trying to acess a field by name
      // from a cursor that references a constant buffer or parameter block,
      // and in these cases we want the access to Just Work.
      //
      case slang::TypeReflection::Kind::ConstantBuffer:
      case slang::TypeReflection::Kind::ParameterBlock: {
        // We basically need to "dereference" the current cursor
        // to go from a pointer to a constant buffer to a pointer
        // to the *contents* of the constant buffer.
        // throw std::runtime_error("TODO: implement dereference for ShaderCursor");
        ShaderCursor d = dereference();
        return d.field(name);
      }
      default: break;
    }

    // If a cursor is pointing at a root shader object (created for a
    // program), then we will also iterate over the entry point shader
    // objects attached to it and look for a matching parameter name
    // on them.
    //
    // This is a bit of "do what I mean" logic and could potentially
    // lead to problems if there could be multiple entry points with
    // the same parameter name.
    //
    // TODO: figure out whether we should support this long-term.
    // uint32_t entryPointCount = m_baseObject->getEntryPointCount();
    // for (uint32_t e = 0; e < entryPointCount; ++e) {
    //   ComPtr<IShaderObject> entryPoint;
    //   m_baseObject->getEntryPoint(e, entryPoint.writeRef());

    //   ShaderCursor entryPointCursor(entryPoint);

    //   auto result = entryPointCursor.getField(name, nameEnd, outCursor);
    //   if (SLANG_SUCCEEDED(result)) return result;
    // }
    throw std::runtime_error(fmt::format("Failed to find field '{}' in ShaderCursor of kind {}",
                                         name, static_cast<int>(typeLayout->getKind())));
  }

  ShaderCursor ShaderCursor::element(int index) {
    switch (typeLayout->getKind()) {
      case slang::TypeReflection::Kind::Array: {
        ShaderCursor elementCursor = child();
        // elementCursor.m_baseObject = m_baseObject;
        elementCursor.typeLayout = typeLayout->getElementTypeLayout();
        elementCursor.offset.uniformOffset =
            offset.uniformOffset +
            index * typeLayout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM);
        elementCursor.offset.rangeIndex = offset.rangeIndex;
        elementCursor.offset.rangeArrayIndex =
            offset.rangeArrayIndex * (uint32_t)typeLayout->getElementCount() + index;
        return elementCursor;
        break;
      }
      case slang::TypeReflection::Kind::Struct: {
        // The logic here is similar to `getField()` except that we don't
        // need to look up the field index based on a name first.
        //
        auto fieldIndex = index;
        slang::VariableLayoutReflection* fieldLayout =
            typeLayout->getFieldByIndex((unsigned int)fieldIndex);
        if (!fieldLayout) return ShaderCursor();

        ShaderCursor fieldCursor = child();
        // fieldCursor.m_baseObject = m_baseObject;
        fieldCursor.typeLayout = fieldLayout->getTypeLayout();
        fieldCursor.offset.uniformOffset = offset.uniformOffset + fieldLayout->getOffset();
        fieldCursor.offset.rangeIndex =
            offset.rangeIndex + (uint32_t)typeLayout->getFieldBindingRangeOffset(fieldIndex);
        fieldCursor.offset.rangeArrayIndex = offset.rangeArrayIndex;
        return fieldCursor;
        break;
      }
      case slang::TypeReflection::Kind::Vector:
      case slang::TypeReflection::Kind::Matrix: {
        ShaderCursor fieldCursor = child();
        // fieldCursor.m_baseObject = m_baseObject;
        fieldCursor.typeLayout = typeLayout->getElementTypeLayout();
        fieldCursor.offset.uniformOffset =
            offset.uniformOffset +
            typeLayout->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM) * index;
        fieldCursor.offset.rangeIndex = offset.rangeIndex;
        fieldCursor.offset.rangeArrayIndex = offset.rangeArrayIndex;
        return fieldCursor;
        break;
      }
      default: break;
    }

    return ShaderCursor();
  }

  ShaderCursor ShaderCursor::dereference() {
    switch (typeLayout->getKind()) {
      case slang::TypeReflection::Kind::ConstantBuffer:
      case slang::TypeReflection::Kind::ParameterBlock: {
        ShaderCursor d = child();
        d.typeLayout = typeLayout->getElementTypeLayout();
        // d.offset.uniformOffset = offset.uniformOffset;
        // d.offset.rangeIndex = offset.rangeIndex;
        // d.offset.rangeArrayIndex = offset.rangeArrayIndex;
        return d;
      }
      default: break;
    }

    // If we can't dereference, return an invalid cursor.
    return {};
  }

  void ShaderCursor::inspect(slang::TypeLayoutReflection* programTypeLayout) {
    if (!isValid()) {
      ImGui::Text("Invalid ShaderCursor");
      return;
    }

    auto containerVarName = typeLayout->getContainerVarLayout()->getName();
    auto elementVarName = typeLayout->getElementVarLayout()->getName();
    auto typeLayoutName = typeLayout->getName();

    inspectSlangBindingRanges(programTypeLayout, typeLayout);
    // ImGui::Text("Binding: %zu.%zu, byte %zu", offset.rangeIndex, offset.rangeArrayIndex,
    //             offset.uniformOffset);
    // ImGui::Text("Kind = %d, Hash=%zx", static_cast<int>(typeLayout->getKind()), hash());

    u32 fieldCount = typeLayout->getFieldCount();
    for (u32 i = 0; i < fieldCount; ++i) {
      auto fieldVarLayout = typeLayout->getFieldByIndex(i);
      auto fieldTypeLayout = fieldVarLayout->getTypeLayout();
      const char* fieldName = fieldVarLayout->getName();

      auto childCursor = this->field(fieldName);  // Kinda a redundant lookup but who cares
      if (ImGui::TreeNode(
              fmt::format("{}   {} : {}", fieldName ? fieldName : "<unnamed>",
                          fieldTypeLayout->getName() ? fieldTypeLayout->getName() : "Unknown",
                          childCursor.formatOffset())
                  .c_str())) {
        childCursor.inspect(programTypeLayout);
        ImGui::TreePop();
      }
    }

    // Arrays
    if (typeLayout->isArray()) {
      u32 elementCount = typeLayout->getElementCount();

      if (elementCount == 0) {
        ImGui::Text("Array of unknown size");

        ShaderCursor elementCursor = this->element(0);
        ImGui::Text("Element 0 : %s", elementCursor.formatOffset().c_str());
        elementCursor.inspect(programTypeLayout);

      } else {
        for (u32 i = 0; i < elementCount; ++i) {
          ShaderCursor elementCursor = this->element(i);
          if (ImGui::TreeNode(fmt::format("[{}] : {}", i, elementCursor.formatOffset()).c_str())) {
            elementCursor.inspect(programTypeLayout);
            ImGui::TreePop();
          }
        }
      }
    }


    auto d = dereference();
    if (d.isValid() && (d.hash() != hash())) {
      if (ImGui::TreeNode("Dereferenced")) {
        d.inspect(programTypeLayout);
        ImGui::TreePop();
      }
    }
  }

}  // namespace ren