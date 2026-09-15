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
  // p += fract(sin(p + float2(13, 7)) * 5e5) * .03 - .015;

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
  // output.color = float4(0, 0, 0, 1);

  float4 textColor = float4(1, 0, 0, 1);

  output.color = lerp(output.color, textColor, PrintValueRight(uv, 4, 2, time));
  return output;
}

// fixed-point print adapted from P_Malin https://www.shadertoy.com/view/4sBSWW
// converted to HLSL, font glyphs flipped vertically, use integer types, less compact style
//   @param StringCoords  position and scale of text. Numbers are 1x1 units in size, right-justified at 0,0
//   @param Value         float value to print
//   @return  0 for non-text, 1 for text values.

const static float2 screenResolution = float2(1920, 1080);
const static float2 fontSize = float2(8.0, 15.0);
const static float2 cellSize = fontSize + float2(0.0, 1.0);
const static uint2 cellCount = uint2(screenResolution / cellSize);
float PrintValue(float2 screenUV, uint2 cellLocation, int DecimalPlaces, float Value) {

  float2 fragCoordPixel = float2(screenUV.x * screenResolution.x, screenUV.y * screenResolution.y);

  uint2 cellInd = uint2(fragCoordPixel / cellSize);

  int NegativeValue = Value < 0.0 ? 1 : 0;
  Value = abs(Value);
  int Digits = max(int(log2(Value) / log2(10.0)), 0) + 1;
  int Decimal = DecimalPlaces > 0 ? 1 : 0;
  int iValue = int(floor(Value * pow(10., DecimalPlaces)));

  uint2 shift = uint2(Digits + Decimal + DecimalPlaces, 0);
  shift = 0;

  float2 StringCoords = (fragCoordPixel - ((cellLocation + shift) * cellSize)) / fontSize;
  if ((StringCoords.y < 0.0) || (StringCoords.y >= 1.0))
    return 0.0;
  int iCoord = -int(floor(StringCoords.x));

  int CharBin = 0, DigitIndex = 0;
  if (iCoord < 0)
    return 0.; // right of number
  else if (iCoord < DecimalPlaces)
    DigitIndex = iCoord; // fractional digit
  else if (iCoord == DecimalPlaces && bool(DecimalPlaces))
    CharBin = 131072; // decimal point
  else if (iCoord < Digits + Decimal + DecimalPlaces)
    DigitIndex = iCoord - Decimal; // integer digit
  else if (iCoord < NegativeValue + Digits + Decimal + DecimalPlaces)
    CharBin = 1792; // minus sign
  else
    return 0.; // left of number

  // has to be a digit
  if (CharBin == 0) {
    const int digits[] = { 480599, 139810, 464711, 476999, 280405, 476951, 481047, 279623, 481111, 477015 };
    int Digit = int(iValue / pow(10, DigitIndex)) % 10;
    CharBin = digits[Digit];
  }

  // convert binary to 4x5 array of points
  int2 DigitCoord = int2(int(frac(StringCoords.x) * 4), int(StringCoords.y * 5));
  return float((CharBin >> (DigitCoord.x + DigitCoord.y * 4)) & 1);
}

float PrintValueRight(float2 screenUV, uint line, int DecimalPlaces, float Value) {
  return PrintValue(screenUV, uint2(cellCount.x - 1, line), DecimalPlaces, Value);
}
