#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D albedo;
layout(set = 0, binding = 1) uniform BlitConfig {
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

void main() {
  vec3 hdr_color = texture(albedo, uv).rgb;
  hdr_color *= config.exposure;

  vec3 tonemapped = aces(hdr_color);
  tonemapped = hdr_color;

  vec3 srgb = linear_to_srgb(tonemapped);

  // Apply ordered dithering to reduce banding
  // Dither value is scaled to 1/256 which is imperceptible but effective at breaking up bands
  srgb += (bayer2x2(gl_FragCoord.xy) - 0.5) / config.ditherDivide;


  outColor = vec4(srgb, 1.0);
}