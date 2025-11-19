#version 450

// From uniform buffer
layout(push_constant, std140) uniform constants {
  mat4 model;
  mat4 view;
  mat4 proj;
  mat4 normalMatrix;
}
pc;

// From vertex buffer (and clip stuff)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;


void main() {
  gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0f);
}