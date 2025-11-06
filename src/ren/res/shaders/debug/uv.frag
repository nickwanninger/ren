#version 450

// This shader simply outputs the UV coordinates as colors for debugging purposes.
layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outColor;

void main() { outColor = vec4(uv, 0.0, 1.0); }