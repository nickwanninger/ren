import ren.core;

[__AttributeUsage(_AttributeTargets.Var)]
struct ColorAttribute {};

namespace glorp {
  struct Foo<T> {
    T value;
  }
}


struct Material {
    [Source("default-white.png")]
    TextureHandle textureIndex;
    [Color]
    float4 color;
    [FloatRange(0.0, 1.0)]
    float opacity;

    glorp.Foo<int> foo;
};


[[vk::push_constant]]
MaterialRoot<Material> root;

[shader("compute")]
[numthreads(1, 1, 1)]
public void printMain()
{

}
