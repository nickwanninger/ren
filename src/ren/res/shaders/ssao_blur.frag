#version 450


layout(binding = 0) uniform sampler2D ssao;
layout(binding = 1) uniform sampler2D depth;
layout(binding = 2) uniform sampler2D normal;

layout(location = 0) in vec2 uvCoords;

layout(location = 0) out vec4 outColor;

// Joint-bilateral filter for SSAO using normal and depth guidance
void main() {
  vec2 texelSize = 1.0 / vec2(textureSize(depth, 0));
  vec2 ssaoTexelSize = 1.0 / vec2(textureSize(ssao, 0));

  texelSize = ssaoTexelSize;

  float centerSSAO = texture(ssao, uvCoords).r;
  float centerDepth = texture(depth, uvCoords).r;
  vec3 centerNormal = normalize(texture(normal, uvCoords).rgb);

  float result = 0.0;
  float weightSum = 0.0;

  // Spatial sigma (in texels)

  // Range sigmas for bilateral filtering
  float sigmaDepth = 0.1;    // Depth difference threshold
  float sigmaNormal = 1.0;   // Normal dot product threshold
  float sigmaSpatial = 1.5;  // Spatial distance threshold

  const int KERNEL_RADIUS = 2;

  for (int x = -KERNEL_RADIUS; x <= KERNEL_RADIUS; ++x) {
    for (int y = -KERNEL_RADIUS; y <= KERNEL_RADIUS; ++y) {
      vec2 offset = vec2(float(x), float(y));  //  * texelSize;
      // vec2 sampleUV = uvCoords + offset;

      // Sample textures
      float sampleSSAO = texture(ssao, uvCoords + offset * ssaoTexelSize).r;
      float sampleDepth = texture(depth, uvCoords + offset * texelSize).r;
      vec3 sampleNormal = normalize(texture(normal, uvCoords + offset * texelSize).rgb);

      // // Spatial distance weight (Gaussian)
      float spatialDist = length(vec2(float(x), float(y)));
      float spatialWeight = exp(-(spatialDist * spatialDist) / (2.0 * sigmaSpatial * sigmaSpatial));

      // // Depth difference weight
      float depthDiff = abs(sampleDepth - centerDepth);
      float depthWeight = exp(-(depthDiff * depthDiff) / (2.0 * sigmaDepth * sigmaDepth));

      // Normal similarity weight (using dot product)
      float normalSim = max(0.0, dot(sampleNormal, centerNormal));
      float normalWeight = exp(-(1.0 - normalSim * normalSim) / (2.0 * sigmaNormal * sigmaNormal));

      // Combined weight
      float weight = spatialWeight * depthWeight * normalWeight;

      result += sampleSSAO * weight;
      weightSum += weight;
    }
  }


  // result = (weightSum > 0.001) ? (result / weightSum) : centerSSAO;
  // outColor = vec4(vec3(centerSSAO), 1.0);
  result = result / weightSum;
  outColor = vec4(vec3(result), 1.0);
}
