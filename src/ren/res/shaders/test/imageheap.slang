import ren.core;

struct Args {
  uint image;
  uint sampler;
  float4 *out;
};

[[vk::push_constant]]
uniform Args pc;

[shader("compute")]
[numthreads(32, 1, 1)]
void sample(uint3 id: SV_DispatchThreadID) {
  if (id.x >= 1)
    return;

  let tex = getTexture2D(pc.image);
  let samp = getSampler(pc.sampler);

  // Touch set 0 as part of the smoke test. A fresh SubmissionUnit provides
  // zero-initialized globals, so this preserves the sampled value.
  pc.out[0] = tex.SampleLevel(samp, float2(0, 0), 0.0) + frameGlobals.time;
  // pc.out[0] = float4(0, 1, 2, 3);
}
