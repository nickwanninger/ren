
public struct Test {
  public RWStructuredBuffer<float> outputBuffer;
  public float value;
}


ParameterBlock<Test> test;

[shader("compute")]
[numthreads(1024, 1, 1)]
void computeMain(uint3 id: SV_DispatchThreadID) {
  if (id.x > test.outputBuffer.getCount()) return;
  test.outputBuffer[id.x] = test.value;
}
