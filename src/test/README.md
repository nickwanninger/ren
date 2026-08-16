# REN tests

Run the suite from the repository root:

```sh
make test
```

The Make target configures CMake with `BUILD_TESTING=ON`, builds only the
`ren-tests` aggregate target, and runs CTest. GoogleTest is an optional
dependency discovered with `find_package(GTest QUIET)`. If it is unavailable,
configuration and `make test` succeed without building a test executable.
Provide a package through the normal CMake search path, `CMAKE_PREFIX_PATH`, or
`GTest_DIR`; the project intentionally does not download GoogleTest.

## Reflection harness

`SlangTestHarness` compiles inline source with
`ISession::loadModuleFromSourceString()`. `CompiledSlangProgram` exposes:

- `layout()` for Slang's linked `ProgramLayout`;
- `entryPointSpirv()` for the generated SPIR-V bytes.

The harness defaults to `SLANG_OPTIMIZATION_LEVEL_NONE` so unused descriptors
remain available for exact comparisons. Pass `SlangTestOptions` with
`SLANG_OPTIMIZATION_LEVEL_MAXIMAL` for production-pruning cases.

`ShaderReflectionTest` is the reusable GTest fixture. Its
`reflectSource()` operation:

1. compiles and links the inline source;
2. runs the production `ShaderReflection::parseFromSlang()` implementation;
3. emits SPIR-V for every entry point;
4. runs the production `ShaderReflection::parseFromSpirv()` implementation;
5. independently enumerates physical bindings with SPIRV-Reflect.

`expectBindingsConsistent()` verifies that every physical binding maps back to
both engine representations with the same set, binding, Vulkan descriptor
type, and descriptor count. Use `RequireExactMatch` for unoptimized fixtures
and `AllowPrunedEngineBindings` for fixtures that intentionally contain unused
resources and enable production optimization.

Example:

```cpp
TEST_F(ShaderReflectionTest, MyLayoutCase)
{
    constexpr std::string_view source = R"slang(
        // Slang source...
    )slang";

    ReflectedSlangCase reflected;
    ASSERT_NO_THROW(reflected = reflectSource(source));
    expectBindingsConsistent(reflected);

    // Add case-specific tree, path, offset, or type assertions here.
}
```

`ren-reflection-tests` compiles the production reflection core and
SPIRV-Reflect directly into the headless test binary. It does not link the
renderer or create a Vulkan device. The ImGui inspector is compiled separately
for the renderer.

Future layout tests should additionally compare:

- descriptor set and binding;
- descriptor type and array count;
- stage visibility after merging entry points;
- push-constant offset and size;
- ordinary field byte offsets, sizes, alignment, and strides where SPIR-V
  exposes equivalent data.

With optimization disabled, expected descriptors should match exactly. With
production optimization enabled, SPIR-V may omit unused resources; in that
mode every emitted SPIR-V descriptor must match a Slang binding, while missing
Slang bindings are allowed only when the test establishes that they were
unused and pruned.
