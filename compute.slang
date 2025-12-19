
struct Params {
  RWTexture2D output;
  Sampler2D<float3> samplerHeap[];
};
// uniform float x;
ParameterBlock<Params> params;
uniform ParameterBlock<Params> uparams;


StructuredBuffer<float> foo;
Sampler2D<float3> samplerHeap1[];
Sampler2D<float3> samplerHeap2[];

// 2. Attributes
[shader("compute")]
[numthreads(16, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    
    uint tex = id.z;
    uint2 pixelPos = id.xy;

    uint texWidth, texHeight;
    params.output.GetDimensions(texWidth, texHeight);
    
    if (pixelPos.x >= texWidth || pixelPos.y >= texHeight) 
        return;

    float2 uv = float2(pixelPos) / float2(texWidth, texHeight);
    
    // float3 color = float3(uv.x, uv.y, 0.5);
    float3 color = params.samplerHeap[tex].Sample(uv);

    params.output[pixelPos] = float4(color, 1.0);
}