
const float shadowBias = 0.001;

float sampleShadowMapPCF(vec4 fragPosWorldSpace) {
  vec4 fragPosLightSpace = ssao.lightProj * ssao.lightView * fragPosWorldSpace;
  fragPosLightSpace /= fragPosLightSpace.w;

  vec3 projCoords = fragPosLightSpace.xyz;  // * 0.5 + 0.5;
  projCoords.xy = projCoords.xy * 0.5 + 0.5;

  if (projCoords.z > 1.0) return 0.0;

  float currentDepth = projCoords.z;
  float shadow = 0.0;

  vec2 texelSize = 1.0 / textureSize(shadow_map_sampler, 0);

  // Poisson disk sampling (smoother results, fewer samples)
  const vec2 poissonDisk[16] = vec2[](
      vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
      vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760), vec2(-0.91588581, 0.45771432),
      vec2(-0.81544232, -0.87912464), vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
      vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023),
      vec2(0.79197514, 0.19090121), vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
      vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

  for (int i = 0; i < 16; ++i) {
    vec2 sampleCoord = projCoords.xy + poissonDisk[i] * texelSize * 2.0;
    float closestDepth = texture(shadow_map_sampler, sampleCoord).r;
    shadow += currentDepth < closestDepth + shadowBias ? 1.0 : 0.0;
  }

  shadow /= 16.0;

  return shadow;
}

float sampleShadowMap(vec4 fragPosWorldSpace) {
  vec4 fragPosLightSpace = ssao.lightProj * ssao.lightView * fragPosWorldSpace;
  fragPosLightSpace /= fragPosLightSpace.w;

  vec3 projCoords = fragPosLightSpace.xyz;  // * 0.5 + 0.5;
  projCoords.xy = projCoords.xy * 0.5 + 0.5;


  float closestDepth = texture(shadow_map_sampler, projCoords.xy).r;
  float currentDepth = projCoords.z;

  // Fragment is in shadow if current depth is GREATER than closest depth
  float shadow = currentDepth < closestDepth + shadowBias ? 1.0 : 0.0;

  // Clamp to shadow map bounds
  if (projCoords.z > 1.0) shadow = 0.0;

  return shadow;
}