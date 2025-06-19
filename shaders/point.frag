#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
  vec2 coord = gl_PointCoord - vec2(0.5);
  // if (dot(coord, coord) > 0.25) {
  //   discard;
  // }

  outColor = vec4(fragColor, 0.2f);
}