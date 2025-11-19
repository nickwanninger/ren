// include this, to output to the gbuffer.
// This is *REQUIRED* for a material to be compatible with the deferred renderer.

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMetallicRoughness;