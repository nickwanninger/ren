import ren.core;

// The shared material layout for rasterization and path tracing.
public struct PBRParams {
  public float4 baseColor;
  public float4 emissiveFactor;
  public float metallic;
  public float roughness;
  public float occlusionStrength; // How much occlusionTexture applies (glTF occlusionTexture.strength).

  public float _padding;

  public TextureHandle baseColorTexture;
  public TextureHandle metallicRoughnessTexture;
  public TextureHandle emissiveTexture;
  public TextureHandle normalTexture;
  // R holds the ambient occlusion. glTF often packs it into the same image
  // as metallicRoughnessTexture.
  public TextureHandle occlusionTexture;
}

// Single-scattering GGX with dielectric or metallic Fresnel.
static const float kBrdfInvPi = 0.318309886f;

public float3 pbrFresnel(float cosine, float3 f0) {
  float grazing = 1.0f - saturate(cosine);
  return f0 + (1.0f - f0) * grazing * grazing * grazing * grazing * grazing;
}

public struct PBRBrdf {
  public float3 baseColor;
  public float metallic;
  public float roughness;

  public float3 f0() { return lerp(float3(0.04f), baseColor, metallic); }

  // This probability selects a sampling lobe. It does not change the BRDF.
  public float specularProbability(float nDotV) {
    float3 fresnel = pbrFresnel(nDotV, f0());
    float specular = dot(fresnel, float3(0.2126f, 0.7152f, 0.0722f));
    float diffuse = dot(baseColor, float3(0.2126f, 0.7152f, 0.0722f)) * (1.0f - metallic);
    return diffuse > 0.0f ? clamp(specular / (specular + diffuse), 0.05f, 0.95f) : 1.0f;
  }

  // Returns f * N.L and the density of the full sampling mixture.
  public float3 evaluate(float3 N, float3 V, float3 L, out float pdf) {
    pdf = 0.0f;
    float nDotV = dot(N, V);
    float nDotL = dot(N, L);
    if (nDotV <= 0.0f || nDotL <= 0.0f) {
      return float3(0.0f);
    }

    float3 H = normalize(V + L);
    float nDotH = saturate(dot(N, H));
    float alpha = roughness * roughness;
    float a2 = alpha * alpha;
    float denominator = (1.0f - nDotH * nDotH) + a2 * nDotH * nDotH;
    float distribution = a2 * kBrdfInvPi / (denominator * denominator);
    float smithV = sqrt(a2 + (1.0f - a2) * nDotV * nDotV);
    float smithL = sqrt(a2 + (1.0f - a2) * nDotL * nDotL);
    float visibility = 0.5f / (nDotL * smithV + nDotV * smithL);
    float3 fresnel = pbrFresnel(dot(V, H), f0());
    // Diffuse light crosses the dielectric boundary in both directions.
    // Using only V.H here leaves too much diffuse energy at grazing angles.
    float3 transmissionV = 1.0f - pbrFresnel(nDotV, f0());
    float3 transmissionL = 1.0f - pbrFresnel(nDotL, f0());
    float3 diffuse = transmissionV * transmissionL * (1.0f - metallic) * baseColor * kBrdfInvPi;
    float3 specular = distribution * visibility * fresnel;

    // A visible GGX normal has density D * G1(V) * V.H / N.V.
    // Reflection contributes the Jacobian 1 / (4 * V.H).
    float specularPdf = distribution * 0.5f / (nDotV + smithV);
    pdf = lerp(nDotL * kBrdfInvPi, specularPdf, specularProbability(nDotV));
    return (diffuse + specular) * nDotL;
  }

  // Sample cosine diffuse or the isotropic GGX visible normal distribution.
  // Directions below the surface are null events; do not resample them.
  public float3 sample(float3 N, float3 V, float3 random) {
    float3 up = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    float radius = sqrt(random.y);
    float phi = 6.28318531f * random.z;
    float x = radius * cos(phi);
    float y = radius * sin(phi);
    if (random.x >= specularProbability(saturate(dot(N, V)))) {
      return normalize(T * x + B * y + N * sqrt(max(0.0f, 1.0f - random.y)));
    }

    float alpha = roughness * roughness;
    float3 localV = float3(dot(V, T), dot(V, B), dot(V, N));
    float3 stretchedV = normalize(float3(alpha * localV.xy, localV.z));
    float lengthSquared = dot(stretchedV.xy, stretchedV.xy);
    float3 T1 = lengthSquared > 0.0f
        ? float3(-stretchedV.y, stretchedV.x, 0.0f) / sqrt(lengthSquared) : float3(1, 0, 0);
    float3 T2 = cross(stretchedV, T1);
    float blend = 0.5f * (1.0f + stretchedV.z);
    y = lerp(sqrt(max(0.0f, 1.0f - x * x)), y, blend);
    float3 stretchedH = T1 * x + T2 * y + stretchedV * sqrt(max(0.0f, 1.0f - x * x - y * y));
    float3 localH = normalize(float3(alpha * stretchedH.xy, max(0.0f, stretchedH.z)));
    float3 H = T * localH.x + B * localH.y + N * localH.z;
    return reflect(-V, H);
  }
}
