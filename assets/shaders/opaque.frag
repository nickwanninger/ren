#version 450



layout(location = 0) in vec3 fragEmissive;
layout(location = 1) in vec3 fragAlbedo;
layout(location = 2) in vec3 fragNormal;
layout(location = 4) in vec2 fragTexCoord;

// 
layout(location = 0) out vec4 emissive;
layout(location = 1) out vec4 albedo;
layout(location = 2) out vec4 normal;
layout(location = 3) out vec4 pbr;

void main() {
    emissive = vec4(0.0, 0.0, 0.0, 1.0);
    albedo   = vec4(fragAlbedo, 1.0);
    normal   = vec4(fragNormal, 1.0);
    pbr      = vec4(0.0, 0.0, 1.0, 1.0);
    // outColor = texture(texSampler, fragTexCoord);
}