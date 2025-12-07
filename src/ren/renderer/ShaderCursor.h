#pragma once

// #include <ren/renderer/Texture.h>
#include <ren/renderer/Image.h>
#include <ren/renderer/Buffer.h>
#include <ren/renderer/Sampler.h>
#include <ren/renderer/ShaderProgram.h>
#include <string_view>

#include <slang.h>

namespace ren {

  // This class is *heavily* based on the Shader Cursor concept from the slang
  // documentation (and the slang-rhi library):
  // https://docs.shader-slang.org/en/latest/shader-cursors.html#making-a-multi-platform-shader-cursor

  class ShaderCursor {
   public:
    ShaderCursor(ShaderObject &shaderObject)
        : obj(shaderObject)
        , refl(obj.getReflection()->getRoot()) {}

    // Navigation
    // Given a cursor that points to a location with some aggregate type (a
    // struct or array), an application needs a way to navigate to a part of that
    // aggregate: a field of a struct or an element of an array. The
    // corresponding operations are:
    ShaderCursor field(const char *name);
    ShaderCursor field(u32 index);
    ShaderCursor element(u32 index);


    ShaderCursor operator[](const char *name) { return field(name); }
    ShaderCursor operator[](u32 index) { return element(index); }


    // Writing
    // Once a cursor has been formed that points to a small enough piece of
    // parameter data, such as an individual texture, an application needs a way
    // to write a value for that parameter. The corresponding operations are:
    void write(ref<Image> image, const Sampler &sampler);
    void write(ref<Image> image, VkFilter samplerFilter);
    void write(ref<Buffer> buffer);

    // TODO: This system is *not* well supported right now.\f
    void writeData(const void *data, size_t size);
    // Write some primitive value.
    template <typename T>
    void writeValue(const T &value) {
      writeData(&value, sizeof(value));
    }


   protected:
    VkWriteDescriptorSet createWrite();

   private:
    ShaderObject &obj;
    ShaderReflection::Node *refl;
    // // A command buffer used for buffer updates
    // // and anything that needs to be recorded.
    // VkCommandBuffer m_cmd;

    // A shader cursor acts much like a pointer, but the type of the data being
    // pointed to is determined dynamically rather than statically. Thus, rather
    // than having a template paramter like a C++ smart pointer would, this
    // shader cursor implementation stores the type (and layout) as a field
    // slang::TypeLayoutReflection *m_typeLayout = nullptr;

    // State
    // - Location
    // A shader cursor logically represents a location to which the values of
    // shader parameters can be written. Differnet GPU APIs expose dfferent
    // mechanisms for parameter-passing, and a given feature in a shader
    // codebase might include parameters that get bound to any or all of those
    // mechanisms. In the case of Vulkan, shader parameters can be bound using
    // two mechanisms:
    //  - Bytes in a buffer (ren::Buffer)
    //  - Descriptors in a descriptor set.
    // A shader cursor implementation for vulkan needs to store at least enough
    // state to represent a location to write to for each of these parameter
    // passing mechanisms.

    // ref<Buffer> m_buffer = nullptr;
    // u8 *m_bufferData = nullptr;  // Mapped pointer to buffer data
    // size_t m_byteOffset = 0;
    // VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    // u32 m_bindingIndex;
    // u32 m_bindingArrayElement;

    // Here our example ShaderCursor type has members to represent a location
    // for each of the two parameter-passing mechanisms mentioned above. For
    // ordinary data, there is a buffer and a byte offset into the buffer,
    // representing a location within the buffer. For descriptors, there is a
    // descriptor set, the index of a binding in that descriptor set, and an
    // array index into the bindings at that index; together these fields
    // represent a location within the descriptor set.


    // ---
  };

}  // namespace ren
