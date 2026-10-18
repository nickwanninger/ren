// PCG (permuted congruential generator). Thanks to:
// www.pcg-random.org and www.shadertoy.com/view/XlGcRh
public uint NextRandom(inout uint state) {
  state = state * 747796405 + 2891336453;
  uint result = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
  result = (result >> 22) ^ result;
  return result;
}

public float RandomValue(inout uint state) {
  return NextRandom(state) / 4294967295.0; // 2^32 - 1
}

// Random value in normal distribution (with mean=0 and sd=1)
public float RandomValueNormalDistribution(inout uint state) {
  // Thanks to https://stackoverflow.com/a/6178290
  float theta = 2 * 3.1415926 * RandomValue(state);
  float rho = sqrt(-2 * log(RandomValue(state)));
  return rho * cos(theta);
}

// Calculate a random direction
public float3 RandomDirection(inout uint state) {
  // Thanks to https://math.stackexchange.com/a/1585996
  float x = RandomValueNormalDistribution(state);
  float y = RandomValueNormalDistribution(state);
  float z = RandomValueNormalDistribution(state);
  return normalize(float3(x, y, z));
}

public float2 RandomPointInCircle(inout uint rngState) {
  float angle = RandomValue(rngState) * 2 * 3.1415926;
  float2 pointOnCircle = float2(cos(angle), sin(angle));
  return pointOnCircle * sqrt(RandomValue(rngState));
}

// Calculate a random direction
public float3 RandomHemisphereDirection(float3 normal, inout uint state) {
  float3 dir = RandomDirection(state);
  return dir * sign(dot(normal, dir));
}

