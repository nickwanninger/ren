#version 450

// Taken, mostly, from https://github.com/shabaazxh/Real-Time-Ambient-Occlusion

layout(set = 0, binding = 0) uniform sampler2D depth_sampler;
layout(set = 0, binding = 1) uniform sampler2D normal_sampler;
layout(set = 0, binding = 2) uniform sampler2D noise_sampler;

layout(set = 0, binding = 3, std140) uniform SSAOUniform {
  mat4 projection;
  mat4 inv_projection;
  mat4 normal_matrix;
  vec3 samples[64];
  vec2 screen_size;
  float radius;
  float intensity;
  float bias;
  int num_samples;
  float noise_divide;
  // implicit padding: 4 bytes (std140 requires alignment to 16 bytes for struct)
}
ssao;


layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;



vec3 random_vec3(vec2 uv) {
  vec3 p3 = fract(vec3(uv.xyx) * vec3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xxy + p3.yzz) * p3.zyx);
}

vec3 depthToPosition(float depth, vec2 uc) {
  vec4 clipSpace = vec4(uc * 2.0 - 1.0, depth, 1.0);
  vec4 viewSpace = ssao.inv_projection * clipSpace;
  return viewSpace.xyz / viewSpace.w;
}

//(ZaOniRinku, 2021) Use depth to obtain normal data
vec3 samplePosition(vec2 uc) {
  float depth = texture(depth_sampler, uc).x;
  return depthToPosition(depth, uc);
}


vec3 getRandomVec(vec2 uv) {
  vec2 noiseScale = ssao.screen_size / ssao.noise_divide;
  vec3 randomVec = normalize(texture(noise_sampler, uv * noiseScale).xyz);
  return randomVec;
}



void main() {
  float radius = ssao.radius;
  float bias = ssao.bias;

  // Obtain the fragment view space position
  float depth = texture(depth_sampler, uv).x;
  // If something is *really* far away, assume no occlusion.
  if (depth >= 0.99999) {
    out_color = vec4(1.0);  // No occlusion
    return;
  }

  vec3 viewSpacePos = depthToPosition(depth, uv);

  // vec3 worldSpacePos = vec3(ssao.inv_view * vec4(viewSpacePos, 1.0));

  // Obtain the fragment normal position from view space

  vec3 worldNormal = texture(normal_sampler, uv).xyz * 2.0 - 1.0;
  vec3 viewSpaceNormal = normalize(mat3(ssao.normal_matrix) * worldNormal);

  vec3 randomVec = getRandomVec(uv);

  vec3 tangent = normalize(randomVec - viewSpaceNormal.xyz * dot(randomVec, viewSpaceNormal.xyz));
  vec3 bitangent = cross(viewSpaceNormal.xyz, tangent);
  mat3 TBN = mat3(tangent, bitangent, viewSpaceNormal.xyz);

  vec3 plane = getRandomVec(uv) - vec3(1.0);

  float occlusion = 0.0;
  for (int i = 0; i < ssao.num_samples; ++i) {
    vec3 samplePos = ssao.samples[i].xyz;           // reflect the sample
    samplePos = TBN * samplePos;                    // convert sample to view-space
    samplePos = viewSpacePos + samplePos * radius;  // offset current position with sample pos

    vec4 offset = vec4(samplePos, 1.0);
    offset = ssao.projection * offset;  // convert .to clip space
    offset.xyz /= offset.w;             // perspective divide
    offset.xy = offset.xy * 0.5 + 0.5;  // convert to texture coordinate (0,1)

    // if it is outside the screen, skip
    if (offset.x < 0.0 || offset.y < 0.0 || offset.x > 1.0 || offset.y > 1.0) { continue; }

    // obtain sample position depth value
    float sampleDepth = samplePosition(offset.xy).z;
    // Range check to ensure within radius
    // float rangeCheck = (samplePos.z - sampleDepth) < radius ? 1.0 : 0.0;
    float rangeCheck = abs(samplePos.z - sampleDepth) < radius ? 1.0 : 0.0;
    // Accumulate occlusion
    occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
  }


  // subtract 1.0 to allow AO to be used with other lighting calculations
  occlusion = 1.0 - (occlusion / float(ssao.num_samples));
  out_color = vec4(occlusion);
}
