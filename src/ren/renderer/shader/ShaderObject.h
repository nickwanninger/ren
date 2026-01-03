#pragma once


#include <ren/types.h>
#include <slang.h>
#include <slang-com-ptr.h>

namespace ren {


  enum class ShaderObjectContainerType { None, Array, StructuredBuffer, ParameterBlock };


  // Abstract base class for the shader object layout.
  class ShaderObjectLayout : public RefCounted<ShaderObjectLayout> {
   public:
    struct BindingRangeInfo {
      // The type of the bindings in this range
      slang::BindingType bindingType;
      u32 count;      // how many bindings in this range
      u32 slotIndex;  // An index into the binding slots array
      // An index into the sub-object array, if this binding range is treated as
      // a sub-object
      u32 subObjectIndex;

      // Is this range specializable?
      bool isSpecializable = false;
    };

    struct SubObjectRangeInfo {
      // Index of the binding range that corresponds to this sub-object
      u32 bindingRangeIndex;
    };


    struct EntryPointInfo {};

    virtual ~ShaderObjectLayout() = default;


   protected:
    slang::TypeLayoutReflection* m_elementTypeLayout = nullptr;
    ShaderObjectContainerType m_containerType = ShaderObjectContainerType::None;

    Slang::ComPtr<slang::ISession> m_slangSession;


   public:
    ShaderObjectContainerType getContainerType() const { return m_containerType; }


    virtual u32 getSlotCount() const = 0;
    virtual u32 getSubObjectCount() const = 0;
    virtual u32 getBindingRangeCount() const = 0;
    virtual const BindingRangeInfo& getBindingRange(u32 index) const = 0;
    virtual u32 getSubObjectRangeCount() const = 0;
    virtual const SubObjectRangeInfo& getSubObjectRange(u32 index) const = 0;
    virtual ref<ShaderObjectLayout> getSubObjectRangeLayout(u32 index) const = 0;


    virtual u32 getEntryPointCount() const { return 0; }
    virtual const EntryPointInfo& getEntryPoint(u32 index) const {
      REN_ASSERT(false);
      // SLANG_RHI_ASSERT_FAILURE("no entrypoints");
      static EntryPointInfo dummy = {};
      return dummy;
    }

    virtual ref<ShaderObjectLayout> getEntryPointLayout(u32 index) const { return nullptr; }
    virtual slang::TypeLayoutReflection* getParameterBlockTypeLayout() {
      return m_elementTypeLayout;
    }


    static slang::TypeLayoutReflection* unwrapParameterGroups(
        slang::TypeLayoutReflection* typeLayout, ShaderObjectContainerType& outContainerType);


    void initBase(slang::ISession* session, slang::TypeLayoutReflection* elementTypeLayout);
  };



  // Vulkan specific implementations for shader objects.
  namespace vk {

    struct SimpleBindingOffset {
      u32 binding = 0;            // an offset into a binding set
      u32 bindingSet = 0;         // the descriptor set (binding is an index into this)
      u32 pushConstantRange = 0;  // Offset in push-constant ranges (not bytes)

      SimpleBindingOffset() {}

      SimpleBindingOffset(slang::VariableLayoutReflection* varLayout) {
        if (!varLayout) return;
        bindingSet =
            (u32)varLayout->getBindingSpace(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
        binding = (u32)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
        pushConstantRange =
            (u32)varLayout->getOffset(SLANG_PARAMETER_CATEGORY_PUSH_CONSTANT_BUFFER);
      }

      /// Add any values in the given `offset`
      void operator+=(const SimpleBindingOffset& offset) {
        binding += offset.binding;
        bindingSet += offset.bindingSet;
        pushConstantRange += offset.pushConstantRange;
      }
    };


    /// A representation of the offset at which to bind a shader parameter or sub-object
    struct BindingOffset : SimpleBindingOffset {
      /// Create a default (zero) offset
      BindingOffset() {}

      /// Create an offset from a simple offset
      explicit BindingOffset(const SimpleBindingOffset& offset)
          : SimpleBindingOffset(offset) {}

      /// Create an offset based on offset information in the given Slang `varLayout`
      BindingOffset(slang::VariableLayoutReflection* varLayout)
          : SimpleBindingOffset(varLayout) {}

      /// Add any values in the given `offset`
      void operator+=(const SimpleBindingOffset& offset) {
        SimpleBindingOffset::operator+=(offset);
      }

      /// Add any values in the given `offset`
      void operator+=(const BindingOffset& offset) { SimpleBindingOffset::operator+=(offset); }
    };


    class ShaderObjectLayoutImpl : public ShaderObjectLayout {
     public:
      struct BindingRangeInfo : public ShaderObjectLayout::BindingRangeInfo {
        u32 bindingOffset;
        u32 setOffset;
      };


      using SubObjectRangeOffset = BindingOffset;
      // TODO: is this unused?
      struct SubObjectRangeStride : BindingOffset {
        SubObjectRangeStride() {}
        // TODO: what is this about?
        SubObjectRangeStride(slang::TypeLayoutReflection* typeLayout) {}
      };


      /// Information about a logical binding range as reported by Slang reflection
      struct SubObjectRangeInfo : ShaderObjectLayout::SubObjectRangeInfo {
        /// The layout expected for objects bound to this range (if known)
        ref<ShaderObjectLayoutImpl> layout;

        /// The offset to use when binding the first object in this range
        SubObjectRangeOffset offset;

        /// Stride between consecutive objects in this range
        SubObjectRangeStride stride;
      };

      struct DescriptorSetInfo {
        std::vector<VkDescriptorSetLayoutBinding> vkBindings;
        int32_t space = -1;
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
      };


      u32 m_slotCount = 0;
      u32 m_subObjectCount = 0;

      std::vector<DescriptorSetInfo> m_descriptorSetInfos;
      std::vector<BindingRangeInfo> m_bindingRanges;
      std::vector<SubObjectRangeInfo> m_subObjectRanges;
      std::vector<VkPushConstantRange> m_ownPushConstantRanges;
      u32 m_childPushConstantRangeCount = 0;

      u32 m_childDescriptorSetCount = 0;
      u32 m_totalBindingCount = 0;
      u32 m_totalOrdinaryDataSize = 0;

      static ref<ShaderObjectLayoutImpl> createForElementType(
          slang::ISession* session, slang::TypeLayoutReflection* elementType);

      ~ShaderObjectLayoutImpl() override;

      u32 getOwnDescriptorSetCount() { return u32(m_descriptorSetInfos.size()); }
      const std::vector<DescriptorSetInfo>& getOwnDescriptorSets() { return m_descriptorSetInfos; }
      u32 getChildDescriptorSetCount() { return m_childDescriptorSetCount; }
      u32 getTotalDescriptorSetCount() {
        return getOwnDescriptorSetCount() + getChildDescriptorSetCount();
      }
      u32 getTotalBindingCount() { return m_totalBindingCount; }

      /// Get the list of push constant ranges required to bind the state of this object itself.
      const std::vector<VkPushConstantRange>& getOwnPushConstantRanges() const {
        return m_ownPushConstantRanges;
      }

      /// Get the number of push constant ranges required to bind the state of this object itself.
      u32 getOwnPushConstantRangeCount() { return (u32)m_ownPushConstantRanges.size(); }

      /// Get the number of push constant ranges required to bind the state of the (transitive)
      /// children of this object.
      u32 getChildPushConstantRangeCount() { return m_childPushConstantRangeCount; }

      /// Get the total number of push constant ranges required to bind the state of this object
      /// and its (transitive) children.
      u32 getTotalPushConstantRangeCount() {
        return getOwnPushConstantRangeCount() + getChildPushConstantRangeCount();
      }
      u32 getTotalOrdinaryDataSize() const { return m_totalOrdinaryDataSize; }

      const std::vector<BindingRangeInfo>& getBindingRanges() { return m_bindingRanges; }

      uint32_t getSlotCount() const override { return m_slotCount; }
      uint32_t getSubObjectCount() const override { return m_subObjectCount; }

      u32 getBindingRangeCount() const override { return m_bindingRanges.size(); }
      const BindingRangeInfo& getBindingRange(uint32_t index) const override {
        return m_bindingRanges[index];
      }

      u32 getSubObjectRangeCount() const override { return m_subObjectRanges.size(); }

      const SubObjectRangeInfo& getSubObjectRange(uint32_t index) const override {
        return m_subObjectRanges[index];
      }
      ref<ShaderObjectLayout> getSubObjectRangeLayout(uint32_t index) const override {
        return m_subObjectRanges[index].layout;
      }
      const std::vector<SubObjectRangeInfo>& getSubObjectRanges() { return m_subObjectRanges; }

      slang::TypeReflection* getType() { return m_elementTypeLayout->getType(); }


     protected:
      struct Builder {
        Builder(slang::ISession* session)
            : m_session(session) {}

        // TODO: device?
        slang::ISession* m_session;
        slang::TypeLayoutReflection* m_elementTypeLayout;


        ShaderObjectContainerType m_containerType = ShaderObjectContainerType::None;

        std::vector<BindingRangeInfo> m_bindingRanges;
        std::vector<SubObjectRangeInfo> m_subObjectRanges;

        uint32_t m_slotCount = 0;
        uint32_t m_subObjectCount = 0;
        std::vector<DescriptorSetInfo> m_descriptorSetBuildInfos;
        std::map<uint32_t, uint32_t> m_mapSpaceToDescriptorSetIndex;
        /// The number of descriptor sets allocated by child/descendent objects
        uint32_t m_childDescriptorSetCount = 0;
        /// The total number of `binding`s consumed by this object and its children/descendents
        uint32_t m_totalBindingCount = 0;
        /// The push-constant ranges that belong to this object itself (if any)
        std::vector<VkPushConstantRange> m_ownPushConstantRanges;
        /// The number of push-constant ranges owned by child/descendent objects
        uint32_t m_childPushConstantRangeCount = 0;
        uint32_t m_totalOrdinaryDataSize = 0;

        uint32_t findOrAddDescriptorSet(uint32_t space);

        static VkDescriptorType mapDescriptorType(slang::BindingType slangBindingType);

        void addDescriptorRangesAsValue(slang::TypeLayoutReflection* typeLayout,
                                        const BindingOffset& offset);
        void addDescriptorRangesAsConstantBuffer(slang::TypeLayoutReflection* elementTypeLayout,
                                                 const BindingOffset& containerOffset,
                                                 const BindingOffset& elementOffset);
        void addDescriptorRangesAsPushConstantBuffer(slang::TypeLayoutReflection* elementTypeLayout,
                                                     const BindingOffset& containerOffset,
                                                     const BindingOffset& elementOffset);

        void addBindingRanges(slang::TypeLayoutReflection* typeLayout);

        // True on success.
        void setElementTypeLayout(slang::TypeLayoutReflection* typeLayout);

        ref<ShaderObjectLayoutImpl> build();
      };

      void initFromBuilder(const Builder& builder);
    };



    // TODO: Skippping for now.
    class EntryPointLayout : public ShaderObjectLayoutImpl {
      using Super = ShaderObjectLayoutImpl;

     public:
      struct Builder : Super::Builder {
        Builder(slang::ISession* session)
            : Super::Builder(session) {}

        ref<EntryPointLayout> build();

        void addEntryPointParams(slang::EntryPointLayout* entryPointLayout);

        slang::EntryPointLayout* m_slangEntryPointLayout = nullptr;

        VkShaderStageFlags m_shaderStageFlag;
      };

      void initFromBuilder(const Builder& builder);

      VkShaderStageFlags getShaderStageFlag() const { return m_shaderStageFlag; }

      slang::EntryPointLayout* getSlangLayout() const { return m_slangEntryPointLayout; };

      slang::EntryPointLayout* m_slangEntryPointLayout;
      VkShaderStageFlags m_shaderStageFlag;
    };



    class RootShaderObjectLayoutImpl : public ShaderObjectLayoutImpl {
      using Super = ShaderObjectLayoutImpl;

     public:
      ~RootShaderObjectLayoutImpl() override;

      /// Information stored for each entry point of the program
      struct EntryPointInfo : Super::EntryPointInfo {
        /// Layout of the entry point
        ref<EntryPointLayout> layout;

        /// Offset for binding the entry point, relative to the start of the program
        BindingOffset offset;
      };

      struct Builder : Super::Builder {
        Builder(slang::IComponentType* program, slang::ProgramLayout* programLayout)
            : Super::Builder(program->getSession())
            , m_program(program)
            , m_programLayout(programLayout) {}

        ref<RootShaderObjectLayoutImpl> build();

        void addGlobalParams(slang::VariableLayoutReflection* globalsLayout);

        void addEntryPoint(ref<EntryPointLayout> entryPointLayout);

        slang::IComponentType* m_program;
        slang::ProgramLayout* m_programLayout;
        std::vector<EntryPointInfo> m_entryPoints;
      };

      void initFromBuilder(const Builder& builder);


      u32 findEntryPointIndex(VkShaderStageFlags stage);
      const std::vector<EntryPointInfo>& getEntryPoints() const { return m_entryPoints; }


      static ref<RootShaderObjectLayoutImpl> create(slang::IComponentType* program,
                                                    slang::ProgramLayout* programLayout);

      inline slang::IComponentType* getSlangProgram() const { return m_program; }
      inline slang::ProgramLayout* getSlangProgramLayout() const { return m_programLayout; }

      const std::vector<VkPushConstantRange>& getAllPushConstantRanges() {
        return m_allPushConstantRanges;
      }

      uint32_t getEntryPointCount() const override { return m_entryPoints.size(); }
      const EntryPointInfo& getEntryPoint(uint32_t index) const override {
        return m_entryPoints[index];
      }
      ref<ShaderObjectLayout> getEntryPointLayout(uint32_t index) const override {
        return m_entryPoints[index].layout;
      }



     protected:
      /// Add all the descriptor sets implied by this root object and sub-objects
      void addAllDescriptorSets();

      /// Recurisvely add descriptor sets defined by `layout` and sub-objects
      void addAllDescriptorSetsRec(ShaderObjectLayoutImpl* layout);

      /// Recurisvely add descriptor sets defined by sub-objects of `layout`
      void addChildDescriptorSetsRec(ShaderObjectLayoutImpl* layout);

      /// Add all the push-constant ranges implied by this root object and sub-objects
      void addAllPushConstantRanges();

      /// Recurisvely add push-constant ranges defined by `layout` and sub-objects
      void addAllPushConstantRangesRec(ShaderObjectLayoutImpl* layout);

      /// Recurisvely add push-constant ranges defined by sub-objects of `layout`
      void addChildPushConstantRangesRec(ShaderObjectLayoutImpl* layout);


     private:
      Slang::ComPtr<slang::IComponentType> m_program;
      slang::ProgramLayout* m_programLayout = nullptr;
      std::vector<EntryPointInfo> m_entryPoints;
      // TODO: cache me!!! Use the old system in REN.
      VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
      std::vector<VkDescriptorSetLayout> m_vkDescriptorSetLayouts;
      std::vector<VkPushConstantRange> m_allPushConstantRanges;
      uint32_t m_totalPushConstantSize = 0;
    };


  }  // namespace vk


}  // namespace ren