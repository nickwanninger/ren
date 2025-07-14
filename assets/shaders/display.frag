#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D emissive;
layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 2) uniform sampler2D normal;
layout(set = 0, binding = 3) uniform sampler2D pbr;
layout(set = 0, binding = 4) uniform sampler2D depthStencil;


float random(vec2 c) {
  return fract(sin(dot(c.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 whiteNoiseDither(vec2 uv, float lum) {
  vec3 color = vec3(0.0);

  if (lum < random(uv)) {
      color = vec3(0.0);
  } else {
      color = vec3(1.0);
  }

  return color;
}


const mat4x4 bayerMatrix4x4 = mat4x4(
    0.0,  8.0,  2.0, 10.0,
    12.0, 4.0,  14.0, 6.0,
    3.0,  11.0, 1.0, 9.0,
    15.0, 7.0,  13.0, 5.0
) / 16.0;

const float bayerMatrix8x8[64] = float[64](
    0.0/ 64.0, 48.0/ 64.0, 12.0/ 64.0, 60.0/ 64.0,  3.0/ 64.0, 51.0/ 64.0, 15.0/ 64.0, 63.0/ 64.0,
  32.0/ 64.0, 16.0/ 64.0, 44.0/ 64.0, 28.0/ 64.0, 35.0/ 64.0, 19.0/ 64.0, 47.0/ 64.0, 31.0/ 64.0,
    8.0/ 64.0, 56.0/ 64.0,  4.0/ 64.0, 52.0/ 64.0, 11.0/ 64.0, 59.0/ 64.0,  7.0/ 64.0, 55.0/ 64.0,
  40.0/ 64.0, 24.0/ 64.0, 36.0/ 64.0, 20.0/ 64.0, 43.0/ 64.0, 27.0/ 64.0, 39.0/ 64.0, 23.0/ 64.0,
    2.0/ 64.0, 50.0/ 64.0, 14.0/ 64.0, 62.0/ 64.0,  1.0/ 64.0, 49.0/ 64.0, 13.0/ 64.0, 61.0/ 64.0,
  34.0/ 64.0, 18.0/ 64.0, 46.0/ 64.0, 30.0/ 64.0, 33.0/ 64.0, 17.0/ 64.0, 45.0/ 64.0, 29.0/ 64.0,
  10.0/ 64.0, 58.0/ 64.0,  6.0/ 64.0, 54.0/ 64.0,  9.0/ 64.0, 57.0/ 64.0,  5.0/ 64.0, 53.0/ 64.0,
  42.0/ 64.0, 26.0/ 64.0, 38.0/ 64.0, 22.0/ 64.0, 41.0/ 64.0, 25.0/ 64.0, 37.0/ 64.0, 21.0 / 64.0
);

vec3 orderedDither(vec2 uv, float lum) {

    float bias = 0.2;
    vec3 color = vec3(0.0);
    vec2 resolution = vec2(1920, 1080);

    float threshold = 0.0;

    int x = int(uv.x * resolution.x) % 8;
    int y = int(uv.y * resolution.y) % 8;
    threshold = bayerMatrix8x8[y * 8 + x];
    // int x = int(uv.x * resolution.x) % 4;
    // int y = int(uv.y * resolution.y) % 4;
    // threshold = bayerMatrix4x4[y][x];

    if (lum < threshold + bias) {
        color = vec3(0.0);
    } else {
        color = vec3(1.0);
    }

    return color;
}

vec3 dither(vec2 uv, vec3 color) {
    float colorNum = 8.0;
    vec2 resolution = vec2(1920, 1080);
    int x = int(uv.x * resolution.x) % 8;
    int y = int(uv.y * resolution.y) % 8;
    float threshold = bayerMatrix8x8[y * 8 + x] - 0.88;

    color.rgb += threshold;
    color.r = floor(color.r * (colorNum - 1.0) + 0.5) / (colorNum - 1.0);
    color.g = floor(color.g * (colorNum - 1.0) + 0.5) / (colorNum - 1.0);
    color.b = floor(color.b * (colorNum - 1.0) + 0.5) / (colorNum - 1.0);

    return color;
}

void main() {
    
    // this is a simple gbuffer shader.

    // sample the albedo
    vec4 albedo = texture(albedo, uv);

    // If the albedo is fully transparent, we can skip rendering this pixel.
    if (albedo.a < 0.01) {
        discard;
    }
    // Sample all the textures
    vec4 emissive = texture(emissive, uv);
    vec4 pbr = texture(pbr, uv);
    vec4 depth = texture(depthStencil, uv);
    vec3 normal = texture(normal, uv).xyz;

    // correct the normal to be in the range [-1, 1]
    normal = normalize(normal * 2.0 - 1.0);


    vec3 lightDirection = normalize(vec3(0.5, 1.0, 0.5)); // Example light direction

    // Calculate the diffuse lighting based on the normal and light direction
    float diffuse = max(dot(normal, lightDirection), 0.0);
    diffuse = mix(0.5, 1, ceil(diffuse * 4 + 0.25) / 4);

    // Calculate the final color by combining albedo, emissive, and diffuse
    vec3 finalColor = albedo.rgb * diffuse + emissive.rgb;


    // Add some dithering
    // finalColor.rgb = dither(uv, finalColor.rgb);

    // For now, just write the normal texture.
    outColor = vec4(finalColor, 1.0);
}