struct Saxpy {
  float a;
  RWStructuredBuffer<float> x;
  RWStructuredBuffer<float> y;

  RWStructuredBuffer<float> out;
}

ParameterBlock<Saxpy> params;

[shader("compute")]
[numthreads(1024, 1, 1)]
void saxpy(uint3 id: SV_DispatchThreadID) {
  if (id.x > params.x.getCount())
    return;
  params.out[id.x] = params.a * params.x[id.x] + params.y[id.x];
}
