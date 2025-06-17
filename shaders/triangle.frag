#version 450

layout(binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;


float linearizeDepth(float depth, float near, float far) {
    // Convert from [0,1] to NDC [-1,1] (if using OpenGL-style projection)
    // For Vulkan default [0,1] range, skip this step
    depth = depth * 2.0 - 1.0;
    
    // Linearize using perspective projection parameters
    return (2.0 * near * far) / (far + near - depth * (far - near));
}

void main() {
    float depth = gl_FragCoord.z;
    // outColor = vec4(fragColor, 1.0f);
    outColor = texture(texSampler, fragTexCoord);
}