#pragma once

#include <ren/renderer/vulkan/Vulkan.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <variant>

namespace ren {
  class Buffer;  // forward declare

  class ParameterCursor;  // forward declare
  class SubmissionUnit;   // forward declare

  // Binding data to shaders is famously hard, so we try to simplify it by leaning on slang's
  // ParameterBlocks, which map directly to vulkan descriptor sets, and are used to group related
  // parameters together. In REN, we use ParameterBlocks to represent both global parameters (e.g.
  // per-frame data) and local parameters (e.g. material data). Each ParameterBlock is represented
  // by a single descriptor set, which is bound to the pipeline before drawing/dispatching.
  // From the C++ side, we represent ParameterBlockData using regular structs which extend
  // ParameterBlock.
  //
  // This class
  class ParameterBlockData {
   public:
    virtual ~ParameterBlockData() = default;


    // The main function of ParameterBlock is to write its data into a
    // Cursor, which allows for a clean way to update descriptor sets.
    // For example, a shader might have this code:
    //   struct Material {
    //     float3 color;
    //     Texture2D albedoMap;
    //   }
    //   ParameterBlock<Material> material;
    //
    // Then, on the C++ side, we would have a struct:
    //   struct Material : public ParameterBlock {
    //     glm::vec3 color;
    //     ref<Texture> albedoMap;
    //   }
    // which implements writeInto like this:
    //   void Material::writeInto(ParameterCursor& cursor) {
    //     cursor["color"].set(color); // Simply a write into a uniform buffer
    //     cursor["albedoMap"].set(albedoMap->getView()); // vkUpdateDescriptorSets(...)
    //   }
    //
    // It would then be used this like this from elsewhere:
    //  ShaderObject obj = ...; // obtained from binding a shader program
    //  Material mat = { .color = COLOR, .albedoMap = ... };
    //  obj.setParameterBlock("material", material); // writes the data into the cursor
    //
    // This function should return true on success, false on failure.
    virtual bool writeInto(ParameterCursor& cursor) = 0;
  };

  using ResourceVariant = std::variant<ref<Buffer>>;  // TODO: Add Texture/Sampler


  // This class represents the GPU state to wrap up a parameter block in a shader.
  // TODO: RHI
  class ParameterBlock : public RefCounted<ParameterBlock> {
    // TODO: this needs to have a few things:
    // - DescriptorSet handle.
    // - Layout info, probably
    // - an array of bound resources. This is so we can track what is bound where, and update
    //   things like buffers by copying data into them.

   public:
   public:
    // Allcate a new ParameterBlock for the given layout, along with a node.
    ParameterBlock(SubmissionUnit& unit, ref<ShaderReflection> refl, ren::ShaderReflection::Node& node, VkDescriptorSetLayout layout);

    void writeUniform(u32 offset, const void* data, size_t size);

    void set(u32 binding, ref<Buffer> buffer);

   protected:
    friend class ParameterCursor;
    friend class ShaderObject;
    ref<ShaderReflection> refl;
    ren::ShaderReflection::Node& node;

    // TODO: allow these to exist longer than per frame, which means we need to
    // abstract descriptor sets to allow them to not all just be freed at the
    // start of a frame with a VkDescriptorPool reset.
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    ref<Buffer> implicitBuffer;
    std::unordered_map<u32, ResourceVariant> boundResources;
    SubmissionUnit& unit;
  };



  // This class represents a cursor into some shader's binding space.
  // We root it all using the information from ShaderReflection, walking the datastructures
  // to find fields, array elements, etc.
  class ParameterCursor {
   public:
    // TODO: accept a CommandEncoder. (or just the ShaderObject?)
    ParameterCursor(ParameterBlock& block, ShaderReflection::Node& node);

    ParameterCursor field(const char* name);
    ParameterCursor element(int index);

    inline ParameterCursor operator[](const char* name) { return this->field(name); }

    // TODO: this is going to get more comlicated as we add more offset info.
    bool operator=(const ParameterCursor& o) const {
      // Simply, compare the underlying nodes.
      return &node == &o.node;
    }


    // Set buffer resource
    void bind(ref<Buffer> buffer);


    // Set uniform data (POD types, structs, etc.)
    template <typename T>
      requires std::is_trivially_copyable_v<T>
    void set(const T& value) {
      setBytes((const void*)&value, sizeof(T));
    }


    // template <typename T>
    //   requires std::is_trivially_copyable_v<T>
    // ParameterCursor& set(const char* name, const T& value) {
    //   auto fieldCursor = this->field(name);
    //   fieldCursor.set<T>(value);
    //   return *this;
    // }

    // template <typename T>
    // ParameterCursor& bind(const char* name, const T& value) {
    //   auto fieldCursor = this->field(name);
    //   fieldCursor.bind<T>(value);
    //   return *this;
    // }


    ParameterBlock& block;
    ShaderReflection::Node& node;

   private:
    void setBytes(const void* data, size_t size);
  };



  // A shader object represents the root of a shader program's binding space. The
  // main functionality of this class is to maintain the link back to the shader
  // program, and dole out ShaderCursors for its fields (parameter blocks or
  // other resources).
  // It also owns the actual descriptor sets and other binding resources needed
  // to bind the shader program for rendering. These are stored in the per-frame
  // arena allocator as a way to ensure that resource lifetime is managed
  // correctly, and things are cleaned up properly.
  //
  // The lifecycle of a ShaderObject is to be craeted with a shader program, then
  // it is bound to a CommandEncoder before drawing or dispatching.
  class ShaderObject {
   public:
    ShaderObject(ref<ShaderProgram> program, SubmissionUnit& unit)
        : program(program)
        , unit(unit) {}

    // TODO: we need to come up with some way to differentiate per-frame
    // "global" parameter blocks vs. per-drawcall blocks. It would be a waste to allocate new

    // The main interface is to get a cursor for a named parameter block.
    // TODO: This should allocate a ParameterBlockBinding as a VkDescriptorSet, after layouts.
    ParameterCursor block(const char* name);


    // TODO: push constant Cursor

    ref<ShaderProgram> program;
    SubmissionUnit& unit;

    // Bind the descriptor sets to the command buffer
    void bind(VkCommandBuffer cmd, VkPipelineBindPoint bindPoint, VkPipelineLayout layout);

   private:
    // Bindings map from Node to ParameterBlockBinding. This is only for the top level blocks.
    std::unordered_map<ren::ShaderReflection::Node*, ren::ParameterBlock*> bindings;
  };



}  // namespace ren
