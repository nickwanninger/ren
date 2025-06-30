#version 450

// From uniform buffer
// layout(push_constant) uniform constants {
//   mat4 model;
//   mat4 view;
//   mat4 proj;
// }
// pc;

// From vertex buffer (and clip stuff)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;


layout(location = 0) out vec3 fragEmissive;
layout(location = 1) out vec3 fragAlbedo;
layout(location = 2) out vec3 fragNormal;
layout(location = 4) out vec2 fragTexCoord;


void main() {

  // place the vertex in screen coordinates.
  gl_Position = vec4(inPosition, 1.0f);

  fragEmissive = vec3(1.0f, 1.0f, 1.0f);
  fragAlbedo = vec3(1.0f, 0.0f, 0.0f);
  fragNormal = inNormal;
  fragTexCoord = inTexCoord;
}