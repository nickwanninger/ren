// Sample an image at two points in a compute shader, and write the colors to
// a buffer that the CPU reads: output[0] at uv (0.25, 0.5), and output[1] at
// uv (0.75, 0.5).

import ren.core;

struct Args {
  TextureHandle image;
  float4 *output;
};

[[vk::push_constant]]
ConstantBuffer<Args> args;

[shader("compute")]
[numthreads(1, 1, 1)]
void readImage(uint3 id: SV_DispatchThreadID) {
  args.output[0] = args.image.SampleLevel(float2(0.25f, 0.5f), 0.0f);
  args.output[1] = args.image.SampleLevel(float2(0.75f, 0.5f), 0.0f);
}
