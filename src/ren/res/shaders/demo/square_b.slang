import ren.core;

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

struct SquareBTransform
{
    float2 center;
    float extent;
    float rotationSpeed;
};

struct SquareBConstants
{
    SquareBTransform transform;
    float4 tint;
    TextureHandle pattern;
};

[[vk::push_constant]]
ConstantBuffer<SquareBConstants> pushConstants;

[shader("vertex")]
Varying vertexMain(Vertex input)
{
    Varying output;
    float angle =
        frameGlobals.time * pushConstants.transform.rotationSpeed;
    float2x2 rotation = float2x2(
        cos(angle), -sin(angle),
        sin(angle), cos(angle));
    output.position = float4(
        mul(rotation, input.position.xy * pushConstants.transform.extent) +
            pushConstants.transform.center,
        input.position.z, 1.0f);
    output.uv = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target
{
    return pushConstants.pattern.Sample(input.uv) * pushConstants.tint;
}
