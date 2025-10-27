#version 450

#include "shaders/engine.glsl"
#include "shaders/skylight.frag"

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;



vec3 getWorldSpaceRay(vec2 uv) {
  // Convert UV [0,1] to NDC [-1,1]
  vec4 clipSpace = vec4(uv * 2.0 - 1.0, 1.0, 1.0);  // Far plane

  // Transform to world space
  vec4 worldSpace = engine.invViewProj * clipSpace;
  worldSpace /= worldSpace.w;

  // Ray direction from engine to this point
  return normalize(worldSpace.xyz - engine.cameraWorldPosition.xyz);
}

vec3 uvToDirection(vec2 uv) {
  float theta = uv.x * 2.0 * 3.14159265;  // azimuth: 0 to 2π
  float phi = uv.y * 3.14159265;          // elevation: 0 to π

  float sinPhi = sin(phi);

  float x = sinPhi * cos(theta);
  float y = cos(phi);
  float z = sinPhi * sin(theta);

  return normalize(vec3(x, y, z));
}

void main() {
  vec3 rayDir = getWorldSpaceRay(fragUV);
  // vec3 rayDir = uvToDirection(fragUV);
  vec3 sunDir = normalize(engine.lightDirection.xyz);

  vec3 skyColor = computeSkyColor(rayDir, sunDir);



  // Apply exposure.
  skyColor = 1.0 - exp(-1.0 * skyColor);

  outColor = vec4(skyColor, 1.0);
}