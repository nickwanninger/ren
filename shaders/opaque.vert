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
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;


layout(location = 0) out vec3 fragEmissive;
layout(location = 1) out vec3 fragAlbedo;
layout(location = 2) out vec3 fragNormal;
layout(location = 4) out vec2 fragTexCoord;

vec4 snap(vec4 vertex, vec2 resolution) {
  vec4 snappedPos = vertex;
  snappedPos.xyz = vertex.xyz / vertex.w;  // convert to normalised device coordinates (NDC)
  snappedPos.xy = floor(resolution * snappedPos.xy) /
                  resolution;  // snap the vertex to the lower-resolution grid
  snappedPos.xyz *= vertex.w;  // convert back to projection-space
  return snappedPos;
}

void main() {

  // place the vertex in screen coordinates.
  gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0f);
  // gl_Position = snap(gl_Position, vec2(320.0f, 240.0f));

  fragEmissive = vec3(1.0f, 1.0f, 1.0f);
  fragAlbedo = vec3(1.0f, 0.0f, 0.0f);

  vec3 worldNormal = normalize(mat3(pc.model) * inNormal);
  // float lambertian = max(0.0f, dot(worldNormal, vec3(0.0f, 0.0f, 1.0f)));
  // lambertian = mix(0.01f, 1.0f, lambertian);
  // fragNormal = vec3(lambertian, lambertian, lambertian);

  fragNormal = worldNormal * 0.5f + 0.5f;  // convert to [0,1] range for the normal map
  // to convert it back, you would do:
  //       normal = encoded * 2.0 - 1.0


  // fragNormal = normalize((pc.model * vec4(inNormal, 1.0f)).xyz);
  fragTexCoord = inTexCoord;
}