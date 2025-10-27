// this file defines the engine-wide glsl structures and functions that are useful for most shaders.
// it also defines some common constants and binding stuff.


#define ENGINE_SET 0
#define PBR_SET 1


// define the layout of the engine uniform buffer object (UBO)
layout(set = ENGINE_SET, binding = 0, std140) uniform EngineUBO {
  mat4 view;
  mat4 proj;
  mat4 invViewProj;  // inverse(proj * view)

  vec4 cameraWorldPosition;
  vec4 lightDirection;
}
engine;