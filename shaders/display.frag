#version 450

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D gbufferEmissive;
layout(set = 0, binding = 1) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 2) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 3) uniform sampler2D gbufferPBR;
layout(set = 0, binding = 4) uniform sampler2D gbufferDepth;

void main() {
    
    // this is a simple gbuffer shader.

    // sample the albedo
    vec4 albedo = texture(gbufferAlbedo, uv);

    // If the albedo is fully transparent, we can skip rendering this pixel.
    if (albedo.a < 0.01) {
        discard;
    }
    // Sample all the textures
    vec4 emissive = texture(gbufferEmissive, uv);
    vec4 pbr = texture(gbufferPBR, uv);
    vec4 depth = texture(gbufferDepth, uv);
    vec3 normal = texture(gbufferNormal, uv).xyz;

    // correct the normal to be in the range [-1, 1]
    normal = normalize(normal * 2.0 - 1.0);


    vec3 lightDirection = normalize(vec3(0.5, 1.0, 0.5)); // Example light direction

    // Calculate the diffuse lighting based on the normal and light direction
    float diffuse = max(dot(normal, lightDirection), 0.0);
    // Calculate the final color by combining albedo, emissive, and diffuse
    vec3 finalColor = albedo.rgb * diffuse + emissive.rgb;


    // For now, just write the normal texture.
    outColor = vec4(finalColor, 1.0);
}