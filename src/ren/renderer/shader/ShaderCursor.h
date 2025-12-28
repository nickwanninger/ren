#pragma once

// #include <ren/renderer/Texture.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/Sampler.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/shader/SlangCompiler.h>  // for slang things, mostly.
#include <string_view>
#include <ren/misc/hash.h>
#include <slang.h>

namespace ren {


  struct ShaderOffset {
    u32 uniformOffset = 0;
    u32 rangeIndex = 0;       // Which binding range?
    u32 rangeArrayIndex = 0;  // Index within the binding range.
  };


  // This class is *heavily* based on the Shader Cursor concept from the slang
  // documentation (and the slang-rhi library):
  // https://docs.shader-slang.org/en/latest/shader-cursors.html#making-a-multi-platform-shader-cursor
  class ShaderCursor {
   public:
    ShaderCursor(slang::TypeLayoutReflection* typeLayout = nullptr);

    bool isValid() const { return typeLayout != nullptr; }


    inline ShaderCursor child() { return ShaderCursor(typeLayout); }

    ShaderCursor field(const char* name);
    ShaderCursor element(int index);

    ShaderCursor dereference();


    void inspect(slang::TypeLayoutReflection* programTypeLayout);  // Somewhat nonsense, as cursors are not really useful alone.

    // TODO: this is going to get more comlicated as we add more offset info.
    bool operator=(const ShaderCursor& o) const {
      return typeLayout == o.typeLayout && offset.uniformOffset == o.offset.uniformOffset &&
             offset.rangeIndex == o.offset.rangeIndex &&
             offset.rangeArrayIndex == o.offset.rangeArrayIndex;
    }

    u64 hash(void) {
      u64 h;
      ren::hash(h, (u64)(uintptr_t)typeLayout);
      ren::hash(h, (u64)offset.uniformOffset);
      ren::hash(h, (u64)offset.rangeIndex);
      ren::hash(h, (u64)offset.rangeArrayIndex);
      return h;
    }


    auto formatOffset() const {
      std::string out;

      out += fmt::format("Binding r:{} #{} Offset {}, soc:{} [", offset.rangeIndex,
                         offset.rangeArrayIndex, offset.uniformOffset,
                         typeLayout->getSubObjectRangeCount()

      );

      for (u32 i = 0; i < typeLayout->getSubObjectRangeCount(); ++i) {
        if (i > 0) out += ", ";
        out += fmt::format("soc{}:{}.{}", i, typeLayout->getSubObjectRangeBindingRangeIndex(i),
                           typeLayout->getSubObjectRangeSpaceOffset(i));
      }

      out += "]";
      u32 slangSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(offset.rangeIndex);


      u32 set = typeLayout->getDescriptorSetSpaceOffset(
          typeLayout->getBindingRangeDescriptorSetIndex(offset.rangeIndex));

      u32 binding = typeLayout->getDescriptorSetDescriptorRangeIndexOffset(
                        slangSetIndex,
                        typeLayout->getBindingRangeFirstDescriptorRangeIndex(offset.rangeIndex)) +
                    offset.rangeArrayIndex;


      out += fmt::format(" (vk {}.{})", set, binding);

      return out;
    }


    slang::TypeLayoutReflection* typeLayout;
    ShaderOffset offset;
  };

}  // namespace ren
