
import ren.core;


// --------------------------------

// The interpolated values that a fullscreen surface gets. The pass owns this
// type (an extern type cannot be part of an entry point signature).
public struct FullscreenVarying {
  // [0, 1] across the viewport. (0, 0) is the first pixel row.
  public float2 uv : TEXCOORD0;
}

public interface IFullscreenSurface {
  // Fullscreen passes don't have a Params associated type.
  // Just use the root push constant block for any parameters.

  // The color for one pixel.
  static float4 shade(FullscreenVarying v);
}


// -----------------------------

// The interpolated values that a surface gets in its fragment functions. The
// pass owns this type
public struct SurfaceVarying {
  // The same fields as the Varying in pbr.slang.
  public float3 worldNormal : NORMAL;
  public float2 uv : TEXCOORD0;
  public float3 worldPos : TEXCOORD1;
  public float3 worldTangent : TEXCOORD2;
  public float3 worldBitangent : TEXCOORD3;
  // Free data for the surface, for values that the other fields do not hold.
  public float4 custom : TEXCOORD4;
}

public interface ISurface {
  // The material parameter struct. MaterialRoot points at an array of these.
  associatedtype Params;

  // Move the vertex and fill the varyings. Write the clip-space position to
  // clipPos. Every pass calls this function, so every pass gets the same
  // geometry. The default is the standard transform. A surface that moves its
  // vertices (for example grass) gives its own version with `override`.
  static SurfaceVarying vertex(Vertex v, ObjectData *obj, Params *mat, out float4 clipPos) {
    clipPos = obj.transformPositionToClipSpace(v.position);
    SurfaceVarying o;
    o.worldNormal = normalize(obj.transformNormal(v.normal));
    o.uv = v.texCoord;
    o.worldPos = mul(obj.modelMatrix, float4(v.position, 1.0)).xyz;
    o.worldTangent = normalize(obj.transformNormal(v.tangent));
    o.worldBitangent = normalize(obj.transformNormal(v.bitangent));
    o.custom = float4(0.0);
    return o;
  }

  // Full shading for the G-buffer pass.
  static GBufferOutput shade(SurfaceVarying v, Params *params);
}
