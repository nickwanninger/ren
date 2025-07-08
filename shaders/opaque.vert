#version 450

// From uniform buffer
layout(push_constant) uniform constants {
  mat4 model;
  mat4 view;
  mat4 proj;
}
pc;

// From vertex buffer (and clip stuff)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;


layout(location = 0) out vec3 fragEmissive;
layout(location = 1) out vec3 fragAlbedo;
layout(location = 2) out vec3 fragNormal;
layout(location = 4) out vec2 fragTexCoord;




layout(set = 0, binding = 0) uniform sampler2D albedoMap;
layout(set = 0, binding = 1) uniform sampler2D normalMap;

layout(set = 1, binding = 0) uniform sampler2D metallicRoughness;
// add a uniform buffer
layout(set = 2, binding = 0) uniform CameraUniforms {
  mat4 view;
  mat4 proj;
  vec3 cameraPos;
} cameraUniforms;


layout(set = 3, binding = 0) uniform sampler2D shadowMap;






void main() {

  gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0f);

  fragEmissive = vec3(1.0f, 1.0f, 1.0f);
  fragAlbedo = vec3(1.0f, 0.0f, 0.0f);

  vec3 worldNormal = normalize(mat3(pc.model) * inNormal);

  fragNormal = worldNormal * 0.5f + 0.5f;  // convert to [0,1] range for the normal map
  // to convert it back, you would do:
  //       normal = encoded * 2.0 - 1.0

  fragTexCoord = inTexCoord;
}