import ren;

struct Vertex
{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float2 texCoord;
    [[vk::location(3)]] float3 tangent;
    [[vk::location(4)]] float3 bitangent;
};

struct Varying
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct SquareAConstants
{
    float2 offset;
    float scale;
    float pulseAmount;
    Texture2D<float4>.Handle image;
    SamplerState.Handle sampler;
};

[[vk::push_constant]]
ConstantBuffer<SquareAConstants> pushConstants;

[shader("vertex")]
Varying vertexMain(Vertex input)
{
    Varying output;
    float pulse = 1.0f + sin(frameGlobals.time) * pushConstants.pulseAmount;
    output.position = float4(
        input.position.xy * pushConstants.scale * pulse + pushConstants.offset,
        input.position.z, 1.0f);
    output.uv = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    return pushConstants.image.Sample(pushConstants.sampler, input.uv);
}
