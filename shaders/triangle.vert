#version 450

// From uniform buffer
layout( push_constant ) uniform constants {
   mat4 model;
   mat4 view;
   mat4 proj;
} pc;

// From vertex buffer (and clip stuff)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;


layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;


void main() {
    gl_Position = pc.proj * pc.view * pc.model * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}