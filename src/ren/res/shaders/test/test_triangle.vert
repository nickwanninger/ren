// frag

#version 450

layout (location = 0) in vec3 color;
layout (location = 0) out vec4 outColor;

void main() {
    outColor = vec4(color, 1.0);
}


// vertex


#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;



layout(location = 0) out vec3 color;


vec3 hslToRgb(vec3 hsl) {
  vec3 rgb = clamp(abs(mod(hsl.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
  rgb = rgb * rgb * (3.0 - 2.0 * rgb);
  return hsl.z + hsl.y * (rgb - 0.5) * (1.0 - abs(2.0 * hsl.z - 1.0));
}

void main() {

  int index = gl_VertexIndex;
  float h = float(index) * 1.61803398874989;
  vec3 hsl = vec3(h, 1.0, 0.5);
  // Sample color based on index,
  color = hslToRgb(hsl);
  gl_Position = vec4(inPosition, 1.0);
}