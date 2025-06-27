#version 450

// layout(binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;


// 
layout(location = 0) out vec4 emissive;
layout(location = 1) out vec4 albedo;
layout(location = 2) out vec4 normal;
layout(location = 3) out vec4 pbr;



void main() {
    emissive = vec4(1.0, 1.0, 1.0, 1.0);
    albedo   = vec4(1.0, 0.0, 0.0, 1.0);
    normal   = vec4(0.0, 1.0, 0.0, 1.0);
    pbr      = vec4(0.0, 0.0, 1.0, 1.0);
    // outColor = texture(texSampler, fragTexCoord);
}