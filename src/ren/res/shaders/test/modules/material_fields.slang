// A surface module for the material file tests. Params has one field of each
// kind that a .material file stores, plus fields that the file skips.

import ren.core;
import ren.surface;


struct MaterialFields : ISurface {

  struct Params {
    float scalar;
    int signedValue;
    uint unsignedValue;
    int64_t signedLarge;
    float2 vector2;
    float3 vector3;
    float4 color;
    uint2 unsignedVector;
    float3x3 matrix3;
    float4x4 matrix4;

    // The file skips names that start with '_'.
    float _padding;
    // Texture fields come in a later step.
    BindlessTexture texture;
  }

  static GBufferOutput shade(SurfaceVarying v, Params *params) {
    GBufferOutput o;
    o.albedo = params.color;
    o.normal = float4(normalize(v.worldNormal) * 0.5 + 0.5, 1.0);
    o.metallicRoughness = float4(0.0, 0.5, 0.0, 1.0);
    return o;
  }
}

export struct Surface : ISurface = MaterialFields;
export struct SurfaceParams = MaterialFields.Params;
