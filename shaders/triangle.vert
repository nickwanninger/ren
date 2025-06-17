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

// vertex: the vertex to be snapped (needs to be in projection-space)
// resolution: the lower resolution, e.g. if my screen resolution is 1280x720, I might choose
// 640x320
vec4 snap(vec4 vertex, vec2 resolution) {
  vec4 snappedPos = vertex;
  snappedPos.xyz = vertex.xyz / vertex.w;  // convert to normalised device coordinates (NDC)
  snappedPos.xy = floor(resolution * snappedPos.xy) /
                  resolution;  // snap the vertex to the lower-resolution grid
  snappedPos.xyz *= vertex.w;  // convert back to projection-space
  return snappedPos;
}

void main() {
  gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0f);
  fragColor = inColor;
  fragTexCoord = inTexCoord;
}