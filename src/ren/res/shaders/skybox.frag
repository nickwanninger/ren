#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;


layout(set = 0, binding = 0, std140) uniform EngineUBO {
  mat4 view;
  mat4 proj;
  mat4 invViewProj;  // inverse(proj * view)

  vec4 cameraWorldPosition;
  vec4 lightDirection;
}
engine;

vec3 getWorldSpaceRay(vec2 uv) {
  // Convert UV [0,1] to NDC [-1,1]
  vec4 clipSpace = vec4(uv * 2.0 - 1.0, 1.0, 1.0);  // Far plane

  // Transform to world space
  vec4 worldSpace = engine.invViewProj * clipSpace;
  worldSpace /= worldSpace.w;

  // Ray direction from engine to this point
  return normalize(worldSpace.xyz - engine.cameraWorldPosition.xyz);
}

vec3 computeSkyColor(vec3 rayDir) {
  float height = rayDir.y;

  if (height >= 0.0) {
    // Minecraft sky: very uniform light blue, barely any gradient
    vec3 skyColor = vec3(0.52, 0.73, 1.0);  // #85B8FF - Minecraft day sky

    // Very subtle darkening toward horizon
    float heightFactor = smoothstep(0.0, 0.3, height);
    skyColor = mix(vec3(0.68, 0.83, 1.0), skyColor, heightFactor);

    // Minecraft sun: bright white circle, hard edge
    vec3 sunDir = normalize(vec3(0.0, 0.8, 0.6));
    float sundot = dot(rayDir, sunDir);

    // Sharp sun disc
    if (sundot > 0.9998) {  // ~1 degree radius
      return vec3(1.0);     // Pure white sun
    }

    // Subtle sun glow (much subtler than realistic)
    float sunGlow = pow(clamp(sundot, 0.0, 1.0), 512.0);
    skyColor += vec3(0.8, 0.8, 0.6) * sunGlow * 0.3;

    return skyColor;
  } else {
    // Minecraft void: dark, desaturated fog color
    float groundDepth = -height;

    // Start with fog color at horizon
    vec3 horizonFog = vec3(0.68, 0.83, 1.0);  // Light blue fog
    vec3 voidColor = vec3(0.17, 0.17, 0.17);  // Dark gray void

    // Sharp transition - Minecraft doesn't blend much
    return mix(horizonFog, voidColor, pow(groundDepth, 0.8));
  }
}


void main() {
  // outColor = vec4(1.0, 0.0, 0.0, 1.0);
  vec3 rayDir = getWorldSpaceRay(fragUV);
  // outColor = vec4(normalize(rayDir) * 0.5 + 0.5, 1.0);
  // // Simple procedural sky
  vec3 skyColor = computeSkyColor(rayDir);
  outColor = vec4(skyColor, 1.0);
}