#version 450

// From uniform buffer
layout(push_constant, std140) uniform constants {
  mat4 model;
  mat4 view;
  mat4 proj;
  mat4 normalMatrix;
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

void main() {
  vec4 clip = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0f);

  // gl_Position = ps1SnapVertex(clip);
  gl_Position = clip;

  mat3 normalMat = mat3(pc.normalMatrix);

  vec3 T = inTangent;
  vec3 N = inNormal;
  vec3 B = inBitangent;
  // re-orthogonalize T with respect to N
  T = normalize(T - dot(T, N) * N);

  float sign = (dot(cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
  B = cross(N, T) * sign;




  worldNormal = normalize(normalMat * N);
  worldTangent = normalize(normalMat * T);
  worldBitangent = normalize(normalMat * B);

  worldPos = vec3(pc.model * vec4(inPosition, 1.0));

  uv = inTexCoord;
}