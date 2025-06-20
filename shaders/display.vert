#version 450

layout(location = 0) out vec2 uv;

void main() {
  // Map gl_VertexIndex to clip space position
  const vec2 pos[4] = vec2[](
      //
      // vec2(-1.0, -1.0),  // bottom left
      // vec2(3.0, -1.0),   // bottom right beyond screen
      // vec2(-1.0, 3.0)    // top left beyond screen


      vec2(-1.0, -1.0),  // top left
      vec2(1.0, -1.0),   // bottom left
      vec2(-1.0, 1.0),   // top right
      vec2(1.0, 1.0)     // bottom right
  );

  gl_Position = vec4(pos[gl_VertexIndex] * 0.9f, 0.0, 1.0);

  // Optional: map to UV space [0, 1]
  uv = (pos[gl_VertexIndex].xy + 1.0) * 0.5;
}