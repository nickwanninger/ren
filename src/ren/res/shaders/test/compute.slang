// struct VSInput {
//   float3 Pos;
//   float3 Normal;
//   float2 UV;
// };

// struct ShaderData {
//   float4x4 projection;
//   float4x4 view;
//   float4x4 model[3];
//   float4 lightPos;
//   uint32_t selected;
// };

// struct VSOutput {
//   float4 Pos : SV_POSITION;
//   float3 Normal;
//   float2 UV;
//   float3 Factor;
//   float3 LightVec;
//   float3 ViewVec;
//   uint32_t InstanceIndex;
// };

// struct Params {

//   uniform ShaderData *shaderData;
//   Sampler2D textures[];
// };

// ParameterBlock<Params> params;

// [shader("vertex")]
// VSOutput main(VSInput input, uint instanceIndex: SV_VulkanInstanceID) {
//   VSOutput output;
//   float4x4 modelMat = params.shaderData->model[instanceIndex];
//   output.Normal = mul((float3x3)mul(params.shaderData->view, modelMat), input.Normal);
//   output.UV = input.UV;
//   output.Pos =
//       mul(params.shaderData->projection, mul(params.shaderData->view, mul(modelMat, float4(input.Pos.xyz, 1.0))));
//   output.Factor = (params.shaderData->selected == instanceIndex ? 3.0f : 1.0f);
//   output.InstanceIndex = instanceIndex;
//   // Calculate view vectors required for lighting
//   float4 fragPos = mul(mul(params.shaderData->view, modelMat), float4(input.Pos.xyz, 1.0));
//   output.LightVec = params.shaderData->lightPos.xyz - fragPos.xyz;
//   output.ViewVec = -fragPos.xyz;
//   return output;
// }

// [shader("fragment")]
// float4 main(VSOutput input) {
//   // Phong lighting
//   float3 N = normalize(input.Normal);
//   float3 L = normalize(input.LightVec);
//   float3 V = normalize(input.ViewVec);
//   float3 R = reflect(-L, N);
//   float3 diffuse = max(dot(N, L), 0.0025);
//   float3 specular = pow(max(dot(R, V), 0.0), 16.0) * 0.75;
//   // Sample from texture
//   float3 color = params.textures[input.InstanceIndex].Sample(input.UV).rgb * input.Factor;
//   return float4(diffuse * color.rgb + specular, 1.0);
// }

struct Foo {
  float *input;
  float *output;
}

uniform float *z;

[shader("compute")]
[numthreads(1, 1, 1)]
void mainA(uniform Foo *foo, uniform float m, uniform float a, Sampler2D tex) {
  *foo.output = *foo.input * m + a + tex.SampleLevel(float2(0, 0), 0.0).x;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void mainB(uniform Foo *foo, uniform float m, uniform float b) { *foo.output = *foo.input * m + b; }
