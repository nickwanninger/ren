#version 450

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D renderTarget;

void main() {
    vec2 uv2 = vec2(
        ceil(uv.x * 320.0f) / 320.0f,
        ceil(uv.y * 180.0f) / 180.0f
    );
    outColor = vec4(uv2.x, uv2.y, 0.0f, 1.0f); // texture(renderTarget, uv);
}