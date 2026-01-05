// This is defined by the kernel. All meshes passed to the GPU must conform to
// this layout. (unless there is a magic system later)
// TODO: Move to a shared header.
struct Vertex {
  // Slang supports standard attributes or explicit binding locations
  [[vk::location(0)]]
  float3 position;
  [[vk::location(1)]]
  float3 normal;
  [[vk::location(2)]]
  float2 texCoord;
  [[vk::location(3)]]
  float3 tangent;
  [[vk::location(4)]]
  float3 bitangent;
};

struct VertexOutput {
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float3 color; // Interpolated color
};

[[vk::push_constant]]
cbuffer PushConstants {
  float brightness;
  float time;
  float stride;
  int index;
  int numDraws;
};

struct Material {
  float3 albedo;
  float metallic;
  float roughness;
  float ao;
  Sampler2D albedoMap;
  Sampler2D metallicMap;
  Sampler2D roughnessMap;
  Sampler2D aoMap;
};

ParameterBlock<Material> mat;

// 3. Helper function (keeping your math logic intact)
float3 hslToRgb(float3 hsl) {
  float3 input = hsl.x * 6.0 + float3(0.0, 4.0, 2.0);
  float3 t = abs(fmod(input, 6.0) - 3.0) - 1.0;
  float3 rgb = clamp(t, 0.0, 1.0);

  // Smoothstep-like interpolation
  rgb = rgb * rgb * (3.0 - 2.0 * rgb);

  return hsl.z + hsl.y * (rgb - 0.5) * (1.0 - abs(2.0 * hsl.z - 1.0));
}

[shader("vertex")]
VertexOutput vertexMain(Vertex input, uint vertexId: SV_VertexID) {
  VertexOutput output;

  // Generate color from Vertex Index
  float h = float(vertexId) * 1.61803398874989;
  float3 hsl = float3(h, 1.0, 0.5);

  output.color = hslToRgb(hsl);

  // Pass position through (Identity)
  output.position = float4(input.position, 1.0);
  output.position.x += float(index) * stride; // Offset based on push constant index

  output.uv = input.texCoord;

  return output;
}

// Warp function. Variations have been around for years. This is
// almost the same as Fabrice's version:
// Fabrice - Plop 2
// https://www.shadertoy.com/view/MlSSDV
float2 W(float2 p) {
  p = (p + 3.) * 4.;
  float t = time / 2.;
  // Layered, sinusoidal feedback, with time component.
  for (int i = 0; i < 3; i++) {
    p += cos(p.yx * 3. + float2(t, 1.57)) / 3.;
    p += sin(p.yx + t + float2(1.57, 0)) / 2.;
    p *= 1.3;
  }

  // A bit of jitter to counter the high frequency sections.
  p += fract(sin(p + float2(13, 7)) * 5e5) * .03 - .015;

  return fmod(p, 2.) - 1.; // Range: [float2(-1), float2(1)]
}

// Bump mapping function. Put whatever you want here. In this case,
// we're returning the length of the sinusoidal warp function.
float bumpFunc(float2 p) {
  return length(W(p)) * .7071; // Range: [0, 1]
}

float3 smoothFract(float3 x) {
  x = fract(x);
  return min(x, x * (1. - x) * 12.);
}

struct FragmentOutput {
  float4 color : SV_Target;
  // float4 color2 : SV_Target1;
};
[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input) {
  // https://www.shadertoy.com/view/4l2XWK
  float resx = 1920.0;

  var uv = input.uv;

  var sp = float3(uv, 0);
  var rd = normalize(float3(uv, 1));
  var lp = float3(cos(time) * .5, sin(time) * .2, -1);
  var sn = float3(0, 0, -1);
  var eps = float2(4. / resx, 0);
  float f = bumpFunc(sp.xy);
  float fx = bumpFunc(sp.xy - eps.xy);
  float fy = bumpFunc(sp.xy - eps.yx);
  const float bumpFactor = .05;
  fx = (fx - f) / eps.x;
  fy = (fy - f) / eps.x;
  sn = normalize(sn + float3(fx, fy, 0) * bumpFactor);
  var ld = lp - sp;
  float lDist = max(length(ld), .0001);
  ld /= lDist;
  float atten = 1. / (1. + lDist * lDist * .15);
  atten *= f * .9 + .1;
  float diff = max(dot(sn, ld), 0.);
  // Enhancing the diffuse value a bit. Made up.
  diff = pow(diff, 4.) * .66 + pow(diff, 8.) * .34;
  float spec = pow(max(dot(reflect(-ld, sn), -rd), 0.), 12.);
  float3 texCol = smoothFract(W(sp.xy).xyy) * .1 + .2;
  float3 col = (texCol * (diff * float3(1, .97, .92) * 2. + .5) + float3(1, .6, .2) * spec * 2.) * atten;
  float ref = max(dot(reflect(rd, sn), float3(1)), 0.);
  col += col * pow(ref, 4.) * float3(.25, .5, 1) * 3.;

  FragmentOutput output;
  output.color = float4(sqrt(clamp(col, 0., 1.)), 1);
  // output.color2 = 1 - output.color;
  return output;
}
