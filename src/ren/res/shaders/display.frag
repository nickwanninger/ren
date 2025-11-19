#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;
layout(set = 0, binding = 1) uniform sampler2D ssao;
layout(set = 0, binding = 2) uniform BlitConfig {
  float exposure;
  float ditherDivide;
}
config;



vec3 aces(vec3 x) {
  float a = 2.51;
  float b = 0.03;
  float c = 2.43;
  float d = 0.59;
  float e = 0.14;
  return (x * (a * x + b)) / (x * (c * x + d) + e);
}

// Linear to sRGB conversion
vec3 linear_to_srgb(vec3 linear) {
  return linear;
  const float gamma = 1.0 / 2.2;
  return pow(linear, vec3(gamma));
}


// Ordered dithering to reduce banding - uses Bayer matrix
float bayer2x2(vec2 pixel_pos) {
  int x = int(pixel_pos.x) % 2;
  int y = int(pixel_pos.y) % 2;
  const float threshold[2][2] = {{0.0 / 4.0, 2.0 / 4.0}, {3.0 / 4.0, 1.0 / 4.0}};
  return threshold[y][x];
}

float ordered_dither_1bit(float value, ivec2 pixel_coord) {
  // 2x2 Bayer matrix (normalized to [0, 1])
  mat2 dither_matrix = mat2(0.0 / 4.0, 2.0 / 4.0, 3.0 / 4.0, 1.0 / 4.0);

  float threshold = dither_matrix[pixel_coord.x % 2][pixel_coord.y % 2];
  return value > threshold ? 1.0 : 0.0;
}

float ordered_dither_1bit_4x4(float value, ivec2 pixel_coord) {
  const mat4 dither_matrix =
      mat4(0.0 / 16.0, 8.0 / 16.0, 2.0 / 16.0, 10.0 / 16.0, 12.0 / 16.0, 4.0 / 16.0, 14.0 / 16.0,
           6.0 / 16.0, 3.0 / 16.0, 11.0 / 16.0, 1.0 / 16.0, 9.0 / 16.0, 15.0 / 16.0, 7.0 / 16.0,
           13.0 / 16.0, 5.0 / 16.0);

  float threshold = dither_matrix[pixel_coord.x % 4][pixel_coord.y % 4];
  return value > threshold ? 1.0 : 0.0;
}

float linearize_depth(float depth, float near, float far) {
  float ndc = depth;  // Vulkan depth is already in [0, 1]
  float view_z = (2.0 * near * far) / (far + near - ndc * (far - near));
  return view_z;
}

void main() {
  vec3 hdr_color = texture(albedo, uv).rgb;
  hdr_color *= texture(ssao, uv).r;

  hdr_color *= config.exposure;

  vec3 tonemapped = aces(hdr_color);

  vec3 srgb = linear_to_srgb(tonemapped);

  // Apply ordered dithering to reduce banding
  // Dither value is scaled to 1/256 which is imperceptible but effective at breaking up bands
  srgb += (bayer2x2(gl_FragCoord.xy) - 0.5) / config.ditherDivide;



  outColor = vec4(srgb, 1.0);
}