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

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec2 uv;
layout(location = 2) out vec3 worldPos;


void main() {

  gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0f);

  worldNormal = normalize(mat3(pc.model) * inNormal) * 0.5f + 0.5f;

  // to convert it back, you would do:
  //       normal = encoded * 2.0 - 1.0

  uv = inTexCoord;
  worldPos = vec3(pc.model * vec4(inPosition, 1.0));
}