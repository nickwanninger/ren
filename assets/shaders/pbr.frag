#version 450



layout(location = 0) in vec3 worldNormal;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 worldPos;

// 
layout(location = 0) out vec4 emissive;
layout(location = 1) out vec4 albedo;
layout(location = 2) out vec4 normal;
layout(location = 3) out vec4 pbr;

#define PBR_SET 0

layout(set = PBR_SET, binding = 0, std140) uniform MaterialUBO {
    vec4 baseColorFactor;
    vec4 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
} material;

layout(set = PBR_SET, binding = 1) uniform sampler2D baseColorTexture;
layout(set = PBR_SET, binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(set = PBR_SET, binding = 3) uniform sampler2D normalTexture;

vec3 getNormalFromMap() {

    vec3 tangentNormal = texture(normalTexture, uv).rgb * 2.0 - 1.0;
    tangentNormal.xy *= 1.0f; // material.normalScale;
    
    vec3 Q1 = dFdx(worldPos);
    vec3 Q2 = dFdy(worldPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);
    
    vec3 N = normalize(worldNormal); // world normal!
    vec3 T = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);
    
    return normalize(TBN * tangentNormal);
}

void main() {
    // Sample everything
    vec4 baseColor = texture(baseColorTexture, uv);
    if (baseColor.a < 0.01) discard;
    vec4 metallicRoughness = texture(metallicRoughnessTexture, uv);
    // vec4 normalMap = texture(normalTexture, uv);

    albedo   = baseColor * material.baseColorFactor;
    emissive = vec4(0.0, 0.0, 0.0, 1.0);
    normal   = vec4(worldNormal, 1.0); // vec4(getNormalFromMap(), 1.0);
    pbr      = vec4(metallicRoughness.r, metallicRoughness.g, 1.0, 1.0);

}