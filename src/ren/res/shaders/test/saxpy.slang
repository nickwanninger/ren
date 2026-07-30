import ren;

struct SaxpyConstants
{
    float a;
    uint length;
    float* x;
    float* y;
    float* output;
};

[[vk::push_constant]]
ConstantBuffer<SaxpyConstants> pushConstants;

[shader("compute")]
[numthreads(256, 1, 1)]
void saxpy(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pushConstants.length)
        return;
    pushConstants.output[id.x] =
        pushConstants.a * pushConstants.x[id.x] + pushConstants.y[id.x];
}
