#version 450

layout(location = 0) in vec3 worldNormal;

layout(location = 0) out vec4 outNormal;


void main() {
  vec3 normal = normalize(worldNormal) * 0.5 + 0.5;
  outNormal = vec4(normal, 1.0);
}