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
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
  float point_size_scale = 2.0f;

  vec4 view_pos = pc.view * pc.model * vec4(inPosition, 1.0f);
  float distance = length(view_pos.xyz);
  gl_Position = pc.proj * view_pos;

  // Inverse distance scaling with min/max bounds
  float size = point_size_scale / max(distance, 0.001);
  gl_PointSize = size;

  fragColor = vec3(1.0f);  // inColor;
  fragTexCoord = inTexCoord;
}