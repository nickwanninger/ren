// REN's native Vulkan shader ABI. All shaders import this module.

public struct FrameGlobals
{
    public float time;
    public float deltaTime;
    public uint frameNumber;
    public uint _padding;
    public float2 renderSize;
    public float2 inverseRenderSize;
};

[vk::binding(0, 0)]
public ConstantBuffer<FrameGlobals> frameGlobals;

// Use Slang's documented typed SPIR-V heap layout. BindlessSpaceIndex selects
// set 1; None keeps sampler, sampled image, and storage image bindings typed.
export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle)
    where T : IOpaqueDescriptor
{
    return defaultGetDescriptorFromHandle(
        handle, BindlessDescriptorOptions.None);
}
