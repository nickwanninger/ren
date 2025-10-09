#version 450

// From uniform buffer
layout(push_constant, std140) uniform constants {
  mat4 model;
  mat4 view;
  mat4 proj;
}
pc;

// From vertex buffer (and clip stuff)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 0) out vec3 worldNormal;
layout(location = 1) out vec2 uv;
layout(location = 2) out vec3 worldPos;
layout(location = 3) out vec3 worldTangent;
layout(location = 4) out vec3 worldBitangent;

// PS1-style vertex snapping parameters
const float SNAP_PIXELS = 240.0; // Number of vertical pixels (e.g. 240 for PS1)

vec4 ps1SnapVertex(vec4 clip) {
    // Perspective divide to NDC
    vec3 ndc = clip.xyz / clip.w;

    // Snap step in NDC space
    float snapStep = 2.0 / SNAP_PIXELS; // NDC goes from -1 to 1

    // Snap X and Y (optionally only Y for vertical snap)
    ndc.x = round(ndc.x / snapStep) * snapStep;
    ndc.y = round(ndc.y / snapStep) * snapStep;

    // Convert back to clip space
    clip.xyz = ndc * clip.w;
    return clip;
}



void main() {

  vec4 clip =  pc.proj * pc.view * pc.model * vec4(inPosition, 1.0f);

  // gl_Position = ps1SnapVertex(clip);
  gl_Position = clip;

  worldNormal = normalize(mat3(pc.model) * inNormal);
  worldTangent = normalize(mat3(pc.model) * inTangent);
  worldBitangent = normalize(mat3(pc.model) * inBitangent);

  worldPos = vec3(pc.model * vec4(inPosition, 1.0));

  uv = inTexCoord;
}