#include <ren/renderer/Descriptors.h>


namespace ren {


  // This is a helper function to create a descriptor pool using the Sizes defined in a given
  // poolSizes struct.
  static VkDescriptorPool createPool(VkDevice device,
                                     const DescriptorAllocator::PoolSizes& poolSizes, int count,
                                     VkDescriptorPoolCreateFlags flags) {
    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(poolSizes.sizes.size());
    for (auto sz : poolSizes.sizes) {
      sizes.push_back({sz.first, uint32_t(sz.second * count)});
    }
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = flags;
    pool_info.maxSets = count;
    pool_info.poolSizeCount = (uint32_t)sizes.size();
    pool_info.pPoolSizes = sizes.data();

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool);

    return descriptorPool;
  }

  DescriptorAllocator::DescriptorAllocator()
      : device(ren::getVulkan().device) {}


  DescriptorAllocator::~DescriptorAllocator() {
    REN_PROFILE_FUNCTION();
    // delete every pool held
    for (auto p : freePools) {
      vkDestroyDescriptorPool(device, p, nullptr);
    }
    for (auto p : usedPools) {
      vkDestroyDescriptorPool(device, p, nullptr);
    }
    usedPools.clear();
    freePools.clear();
    currentPool = VK_NULL_HANDLE;
  }


  VkDescriptorPool DescriptorAllocator::grab_pool() {
    REN_PROFILE_FUNCTION();
    // there are reusable pools availible
    if (freePools.size() > 0) {
      // grab pool from the back of the vector and remove it from there.
      VkDescriptorPool pool = freePools.back();
      freePools.pop_back();
      return pool;
    } else {
      // no pools availible, so create a new one
      return createPool(device, descriptorSizes, ren::descriptorSetPoolSize, 0);
    }
  }

  bool DescriptorAllocator::allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout) {
    REN_PROFILE_FUNCTION();
    // initialize the currentPool handle if it's null
    if (currentPool == VK_NULL_HANDLE) {
      currentPool = grab_pool();
      usedPools.push_back(currentPool);
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;

    allocInfo.pSetLayouts = &layout;
    allocInfo.descriptorPool = currentPool;
    allocInfo.descriptorSetCount = 1;

    // try to allocate the descriptor set
    VkResult allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);
    bool needReallocate = false;

    switch (allocResult) {
      case VK_SUCCESS:
        // all good, return
        return true;
      case VK_ERROR_FRAGMENTED_POOL:
      case VK_ERROR_OUT_OF_POOL_MEMORY:
        // reallocate pool
        needReallocate = true;
        break;
      default:
        // unrecoverable error
        return false;
    }

    if (needReallocate) {
      // allocate a new pool and retry
      currentPool = grab_pool();
      usedPools.push_back(currentPool);

      allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);

      // if it still fails then we have big issues
      if (allocResult == VK_SUCCESS) { return true; }
    }

    return false;
  }


  void DescriptorAllocator::reset() {
    REN_PROFILE_FUNCTION();
    // reset all used pools and add them to the free pools
    for (auto p : usedPools) {
      vkResetDescriptorPool(device, p, 0);
      freePools.push_back(p);
    }

    // clear the used pools, since we've put them all in the free pools
    usedPools.clear();

    // reset the current pool handle back to null
    currentPool = VK_NULL_HANDLE;
  }

  // ------------------------------------------------------ //

  bool DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const {
    if (other.bindings.size() != bindings.size()) {
      return false;
    } else {
      // compare each of the bindings is the same. Bindings are sorted so they will match
      for (int i = 0; i < bindings.size(); i++) {
        if (other.bindings[i].binding != bindings[i].binding) { return false; }
        if (other.bindings[i].descriptorType != bindings[i].descriptorType) { return false; }
        if (other.bindings[i].descriptorCount != bindings[i].descriptorCount) { return false; }
        if (other.bindings[i].stageFlags != bindings[i].stageFlags) { return false; }
      }
      return true;
    }
  }


  size_t DescriptorLayoutInfo::hash() const {
    using std::size_t;
    using std::hash;

    size_t result = hash<size_t>()(bindings.size());

    for (const VkDescriptorSetLayoutBinding& b : bindings) {
      // pack the binding data into a single int64. Not fully correct but it's ok
      size_t binding_hash =
          b.binding | b.descriptorType << 8 | b.descriptorCount << 16 | b.stageFlags << 24;

      // shuffle the packed binding data and xor it with the main hash
      result ^= hash<size_t>()(binding_hash);
    }

    return result;
  }



  void DescriptorLayoutInfo::addBinding(uint32_t binding, VkDescriptorType type, uint32_t count,
                                        VkShaderStageFlags stageFlags,
                                        const VkSampler* imSampler) {
    REN_PROFILE_FUNCTION();
    // create the descriptor binding for the layout
    VkDescriptorSetLayoutBinding newBinding{};
    newBinding.descriptorCount = count;
    newBinding.descriptorType = type;
    newBinding.pImmutableSamplers = imSampler;
    newBinding.stageFlags = stageFlags;
    newBinding.binding = binding;
    // add the binding to the list
    bindings.push_back(newBinding);
  }


  // ------------------------------------------------------ //

  DescriptorLayoutCache::~DescriptorLayoutCache() {
    REN_PROFILE_FUNCTION();


    auto device = ren::getVulkan().device;
    // delete every descriptor layout held
    for (auto pair : layoutCache) {
      vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
    }
  }
  VkDescriptorSetLayout DescriptorLayoutCache::createLayout(DescriptorLayoutInfo& info) {
    auto device = ren::getVulkan().device;
    // sort the bindings if they aren't in order
    std::sort(info.bindings.begin(), info.bindings.end(),
              [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b) {
                return a.binding < b.binding;
              });

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = (uint32_t)info.bindings.size();
    layoutInfo.pBindings = info.bindings.data();

    // try to grab from cache
    auto it = layoutCache.find(info);
    if (it != layoutCache.end()) {
      cacheHits++;
      return (*it).second;
    } else {
      // create a new one (not found)
      VkDescriptorSetLayout layout;
      vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout);

      // add to cache
      layoutCache[info] = layout;
      cacheMisses++;
      return layout;
    }
  }

  VkDescriptorSetLayout DescriptorLayoutCache::createLayout(VkDescriptorSetLayoutCreateInfo* info) {
    REN_PROFILE_FUNCTION();

    DescriptorLayoutInfo layoutinfo;
    layoutinfo.bindings.reserve(info->bindingCount);
    bool isSorted = true;
    int lastBinding = -1;

    // copy from the direct info struct into our own one
    for (int i = 0; i < info->bindingCount; i++) {
      layoutinfo.bindings.push_back(info->pBindings[i]);

      // check that the bindings are in strict increasing order
      if (info->pBindings[i].binding > lastBinding) { lastBinding = info->pBindings[i].binding; }
    }

    return createLayout(layoutinfo);
  }


  // ------------------------------------------------------ //

  DescriptorBuilder::DescriptorBuilder(DescriptorLayoutCache& layoutCache,
                                       DescriptorAllocator& allocator)
      : cache(layoutCache)
      , alloc(allocator) {}


  DescriptorBuilder& DescriptorBuilder::bindBuffer(uint32_t binding,
                                                   VkDescriptorBufferInfo* bufferInfo,
                                                   VkDescriptorType type,
                                                   VkShaderStageFlags stageFlags) {
    REN_PROFILE_FUNCTION();

    // create the descriptor binding for the layout
    VkDescriptorSetLayoutBinding newBinding{};

    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.pImmutableSamplers = nullptr;
    newBinding.stageFlags = stageFlags;
    newBinding.binding = binding;

    bindings.push_back(newBinding);

    // create the descriptor write
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.pBufferInfo = bufferInfo;
    newWrite.dstBinding = binding;

    writes.push_back(newWrite);
    return *this;
  }

  DescriptorBuilder& DescriptorBuilder::bindImage(uint32_t binding,
                                                  VkDescriptorImageInfo* imageInfo,
                                                  VkDescriptorType type,
                                                  VkShaderStageFlags stageFlags) {
    REN_PROFILE_FUNCTION();
    VkDescriptorSetLayoutBinding newBinding{};

    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.pImmutableSamplers = nullptr;
    newBinding.stageFlags = stageFlags;
    newBinding.binding = binding;

    bindings.push_back(newBinding);

    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.pImageInfo = imageInfo;
    newWrite.dstBinding = binding;

    writes.push_back(newWrite);
    return *this;
  }

  bool DescriptorBuilder::build(VkDescriptorSet& set, VkDescriptorSetLayout& layout) {
    // build layout first
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = nullptr;

    layoutInfo.pBindings = bindings.data();
    layoutInfo.bindingCount = bindings.size();

    layout = cache.createLayout(&layoutInfo);

    // allocate descriptor
    bool success = alloc.allocate(&set, layout);
    if (!success) { return false; };

    // write descriptor
    for (VkWriteDescriptorSet& w : writes) {
      w.dstSet = set;
    }

    vkUpdateDescriptorSets(getVulkan().device, writes.size(), writes.data(), 0, nullptr);

    return true;
  }

  bool DescriptorBuilder::build(VkDescriptorSet& set) {
    VkDescriptorSetLayout layout;
    return build(set, layout);
  }

}  // namespace ren