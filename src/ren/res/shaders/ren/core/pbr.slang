import ren.core;
import ren.surface;

export struct Surface : ISurface = PBR;
export struct SurfaceParams = PBR.Params; // This is just required...


struct PBR : ISurface {
  struct Params {
    float4 baseColor;
    float4 emissiveFactor;
    float metallic;
    float roughness;
    float occlusionStrength; // How much occlusionTexture applies (glTF occlusionTexture.strength).

    float _padding;

    TextureHandle baseColorTexture;
    TextureHandle metallicRoughnessTexture;
    TextureHandle emissiveTexture;
    TextureHandle normalTexture;
    // R holds the ambient occlusion. glTF often packs it into the same image
    // as metallicRoughnessTexture.
    TextureHandle occlusionTexture;
  }

  static GBufferOutput shade(SurfaceVarying input, Params *mat) {

    let outColor = mat.baseColorTexture.sampleOr(input.uv, float4(1.0)) * mat.baseColor;
    // Alpha clipping
    if (outColor.a < 0.1)
      discard;

    // Move the normal-map sample from tangent space to world space. An unset
    // handle samples as magenta, so check it first.
    float3 N = normalize(input.worldNormal);
    if (mat.normalTexture.isValid()) {
      float3 T = normalize(input.worldTangent);
      float3 B = normalize(input.worldBitangent);
      float3 n = mat.normalTexture.Sample(input.uv).xyz * 2.0 - 1.0;
      N = normalize(n.x * T + n.y * B + n.z * N);
    }

    // The target is a signed float format, so the normal is stored raw.
    let outNormal = float4(N, 1.0);

    // glTF: G is roughness, B is metallic. Without a texture, the factors
    // alone apply.
    let metallicRoughnessSample = mat.metallicRoughnessTexture.sampleOr(input.uv, float4(1.0));
    float metallic = saturate(metallicRoughnessSample.b * mat.metallic);
    float roughness = saturate(metallicRoughnessSample.g * mat.roughness);
    roughness = max(roughness, 0.05); // avoid 0 roughness

    // glTF: occlusion = 1 + strength * (sample - 1). Without a texture there is
    // no occlusion.
    float occlusionSample = mat.occlusionTexture.sampleOr(input.uv, float4(1.0)).r;
    float occlusion = lerp(1.0, occlusionSample, mat.occlusionStrength);

    // TODO:
    GBufferOutput output;
    output.albedo = outColor;
    output.normal = outNormal;
    output.metallicRoughness = float4(metallic, roughness, occlusion, 1.0);
    return output;
  }
}

