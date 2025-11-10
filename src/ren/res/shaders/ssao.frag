#version 450

// Taken, mostly, from https://github.com/shabaazxh/Real-Time-Ambient-Occlusion

layout(set = 0, binding = 0) uniform sampler2D depth_sampler;
layout(set = 0, binding = 1) uniform sampler2D normal_sampler;
layout(set = 0, binding = 2) uniform sampler2D noise_sampler;

layout(set = 0, binding = 3, std140) uniform SSAOUniform {
  mat4 projection;      // offset 0, 64 bytes
  mat4 inv_projection;  // offset 64, 64 bytes
  vec3 samples[64];     // offset 128, 1024 bytes (each vec3 padded to 16 bytes)
  vec2 screen_size;     // offset 1152, 8 bytes
  float radius;         // offset 1160, 4 bytes
  float bias;           // offset 1164, 4 bytes
  int num_samples;      // offset 1168, 4 bytes
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


//(ZaOniRinku, 2021) Use depth to obtain normal data
vec3 depthToPositions(vec2 tc) {
  float depth = texture(depth_sampler, tc).x;
  vec4 clipSpace = vec4(tc * 2.0 - 1.0, depth, 1.0);
  vec4 viewSpace = ssao.inv_projection * clipSpace;
  return viewSpace.xyz / viewSpace.w;
}



void main() {
  float radius = ssao.radius;
  float bias = ssao.bias;

  // Obtain the fragment view space position
  vec3 viewSpacePositions = depthToPositions(uv);
  // Obtain the fragment normal position from view space
  vec4 viewSpaceNormals = texture(normal_sampler, uv) * 2.0 - 1.0;

  vec2 noiseScale = ssao.screen_size / 4.0;

  // vec3 randomVec = normalize(random_vec3(uv));
  vec3 randomVec = texture(noise_sampler, uv * noiseScale).xyz;


  //(Joey De Vries, 2020) Create a TBN matrix to convert the sample from tangent-space to view-space
  vec3 tangent = normalize(randomVec - viewSpaceNormals.xyz * dot(randomVec, viewSpaceNormals.xyz));
  vec3 bitangent = cross(viewSpaceNormals.xyz, tangent);
  mat3 TBN = mat3(tangent, bitangent, viewSpaceNormals.xyz);

  vec3 plane = texture(noise_sampler, uv * noiseScale).xyz - vec3(1.0);

  float occlusion = 0.0;
  for (int i = 0; i < ssao.num_samples; ++i) {
    vec3 samplePos = reflect(ssao.samples[i].xyz, plane);  // reflect the sample
    samplePos = TBN * samplePos;                           // convert sample to view-space
    samplePos = viewSpacePositions + samplePos * radius;  // offset current position with sample pos

    vec4 offset = vec4(samplePos, 1.0);
    offset = ssao.projection * offset;  // convert .to clip space
    offset.xyz /= offset.w;             // perspective divide
    offset.xy = offset.xy * 0.5 + 0.5;  // convert to texture coordinate (0,1)

    float sampleDepth = depthToPositions(offset.xy).z;  // obtain sample pos depth value
    float rangeCheck = (samplePos.z - sampleDepth) < radius
                           ? 1.0
                           : 0.0;  // range check to ensure within radius
    occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) *
                 rangeCheck;  // check if depth > z pos to determine occlusion
  }
  // subtract 1.0 to allow AO to be used with other lighting calculations
  occlusion = 1.0 - (occlusion / ssao.num_samples);
  out_color = vec4(vec3(occlusion), 1.0);
}
