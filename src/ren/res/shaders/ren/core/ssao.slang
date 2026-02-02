// Ported from ssao.frag
// Taken, mostly, from https://github.com/shabaazxh/Real-Time-Ambient-Occlusion

struct SSAOUniform {
  float4x4 projection;
  float4x4 inv_projection;
  float4x4 normal_matrix;
  float4 samples[64];
  float2 screen_size;
  float radius;
  float intensity;
  float bias;
  int num_samples;
  float noise_divide;
};

struct SSAOParams {
  SSAOUniform u;
  Sampler2D<float> depth;
  Sampler2D<float4> normal;
  Sampler2D<float4> noise;
};

ParameterBlock<SSAOParams> params;

struct Varying {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

[shader("vertex")]
Varying vert(uint vertexID: SV_VertexID) {
  Varying output;
  output.uv = float2((vertexID << 1) & 2, vertexID & 2);
  output.position = float4(output.uv * 2.0f + -1.0f, 0.0f, 1.0f);
  return output;
}

float3 depthToPosition(float depth, float2 uc) {
  float4 clipSpace = float4(uc * 2.0 - 1.0, depth, 1.0);
  float4 viewSpace = mul(params.u.inv_projection, clipSpace);
  return viewSpace.xyz / viewSpace.w;
}

float3 samplePosition(float2 uc) {
  float depth = params.depth.Sample(uc);
  return depthToPosition(depth, uc);
}

float3 getRandomVec(float2 uv) {
  float2 noiseScale = params.u.screen_size / params.u.noise_divide;
  // Map from [0, 1] to [-1, 1]
  float3 randomVec = normalize(params.noise.Sample(uv * noiseScale).xyz * 2.0 - 1.0);
  return randomVec;
}

[shader("fragment")]
float4 frag(Varying input) : SV_Target {
  float radius = params.u.radius;
  float bias = params.u.bias;

  // Obtain the fragment view space position
  float depth = params.depth.Sample(input.uv);

  // If something is *really* far away, assume no occlusion.
  if (depth >= 0.99999) {
    return float4(1.0); // No occlusion
  }

  float3 viewSpacePos = depthToPosition(depth, input.uv);

  // Obtain the fragment normal position from view space
  float3 worldNormal = params.normal.Sample(input.uv).xyz * 2.0 - 1.0;

  // Convert normal to view space
  // Cast float4x4 to float3x3 for normal transformation (rotation only)
  float3x3 normalMatrix = (float3x3)params.u.normal_matrix;
  float3 viewSpaceNormal = normalize(mul(normalMatrix, worldNormal));

  float3 randomVec = getRandomVec(input.uv);

  // Create TBN matrix
  float3 tangent = normalize(randomVec - viewSpaceNormal * dot(randomVec, viewSpaceNormal));
  float3 bitangent = cross(viewSpaceNormal, tangent);
  float3x3 TBN = float3x3(tangent, bitangent, viewSpaceNormal);

  float occlusion = 0.0;
  for (int i = 0; i < params.u.num_samples; ++i) {
    float3 samplePos = params.u.samples[i].xyz;

    // Transform sample to view-space
    samplePos = mul(samplePos, TBN); // Slang/HLSL is usually mul(vector, matrix) if row-major or matrix is TBN?
    // Wait, TBN construction: columns are tangent, bitangent, normal?
    // In GLSL constructor mat3(vec3, vec3, vec3) takes columns.
    // float3x3(row0, row1, row2) in HLSL/Slang?
    // Let's verify matrix construction.
    // GLSL: mat3(col0, col1, col2)
    // Slang: float3x3(row0, row1, row2) usually?
    // Actually, let's just do the multiplication manually or check standard library.
    // standard HLSL float3x3 constructor takes elements.

    // Let's rewrite TBN closer to intent:
    // We want to transform the sample (tangent space) to view space.
    // samplePos = tangent * s.x + bitangent * s.y + normal * s.z
    samplePos =
        tangent * params.u.samples[i].x + bitangent * params.u.samples[i].y + viewSpaceNormal * params.u.samples[i].z;

    samplePos = viewSpacePos + samplePos * radius;

    float4 offset = float4(samplePos, 1.0);
    offset = mul(params.u.projection, offset); // convert to clip space
    offset.xyz /= offset.w;                    // perspective divide
    offset.xy = offset.xy * 0.5 + 0.5;         // convert to texture coordinate (0,1)

    // if it is outside the screen, skip
    if (offset.x < 0.0 || offset.y < 0.0 || offset.x > 1.0 || offset.y > 1.0) {
      continue;
    }

    // obtain sample position depth value
    float sampleDepth = samplePosition(offset.xy).z; // samplePosition returns view space position

    // Range check to ensure within radius
    float rangeCheck = abs(samplePos.z - sampleDepth) < radius ? 1.0 : 0.0;

    // Accumulate occlusion
    // in GLSL: (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0)
    // Note: View space Z is negative in some setups or positive in others.
    // ssao.frag used: sampleDepth >= samplePos.z + bias
    // In this renderer, likely using standard GL conventions where -Z is forward?
    // Use the same logic as source for safety.
    occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
  }

  occlusion = 1.0 - (occlusion / float(params.u.num_samples));
  return float4(occlusion);
}
