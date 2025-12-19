// This is defined by the kernel. All meshes passed to the GPU must conform to this layout.
// (unless there is a magic system later)
// TODO: Move to a shared header.
struct Vertex
{
    // Slang supports standard attributes or explicit binding locations
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float2 texCoord;
    [[vk::location(3)]] float3 tangent;
    [[vk::location(4)]] float3 bitangent;
};


struct VertexOutput
{
    float4 position : SV_Position;
    float3 color; // Interpolated color
};

[[vk::push_constant]]
cbuffer PushConstants {
    float brightness;
    float stride;
    int index;
    int numDraws;
};


struct Material {
    float3 albedo;
    float metallic;
    float roughness;
    float ao;
    Sampler2D albedoMap;
    Sampler2D metallicMap;
    Sampler2D roughnessMap;
    Sampler2D aoMap;
};

ParameterBlock<Material> mat;

// 3. Helper function (keeping your math logic intact)
float3 hslToRgb(float3 hsl)
{
    float3 input = hsl.x * 6.0 + float3(0.0, 4.0, 2.0);
    float3 t = abs(fmod(input, 6.0) - 3.0) - 1.0;
    float3 rgb = clamp(t, 0.0, 1.0);
    
    // Smoothstep-like interpolation
    rgb = rgb * rgb * (3.0 - 2.0 * rgb);
    
    return hsl.z + hsl.y * (rgb - 0.5) * (1.0 - abs(2.0 * hsl.z - 1.0));
}

[shader("vertex")]
VertexOutput vertexMain(Vertex input, uint vertexId : SV_VertexID) {
    VertexOutput output;

    // Generate color from Vertex Index
    float h = float(vertexId) * 1.61803398874989;
    float3 hsl = float3(h, 1.0, 0.5);

    output.color = hslToRgb(hsl);
    
    // Pass position through (Identity)
    output.position = float4(input.position, 1.0);
    output.position.x += float(index) * stride; // Offset based on push constant index

    return output;
}




[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float indexNorm = float(index) / float(numDraws);
    float e = 1.0 - indexNorm;
    return float4(input.color * brightness * e, 1.0);
}






// RWTexture2D<float4> outputTexture;

// [shader("compute")]
// [numthreads(16, 16, 1)]
// void computeMain(uint3 id : SV_DispatchThreadID)
// {
//     uint2 pixelPos = id.xy;

//     uint texWidth, texHeight;
//     outputTexture.GetDimensions(texWidth, texHeight);
    
//     if (pixelPos.x >= texWidth || pixelPos.y >= texHeight) 
//         return;

//     float2 uv = float2(pixelPos) / float2(texWidth, texHeight);
    
//     float3 color = float3(uv.x, uv.y, 0.5);

//     outputTexture[pixelPos] = float4(color, 1.0);
// }