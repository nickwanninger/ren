add_executable(ren-reflection-tests
  ShaderReflectionHarnessTest.cpp
  ShaderReflectionTestHarness.cpp
  ShaderReflectionTestHarness.h
  SlangTestHarness.cpp
  SlangTestHarness.h
  TestLogging.cpp

  # Compile the production reflection implementation into the headless test
  # binary. UI inspection lives in ShaderReflectionInspect.cpp and is
  # intentionally not part of this target.
  ${CMAKE_SOURCE_DIR}/src/ren/renderer/shader/ShaderReflection.cpp

  # SPIRV-Reflect is compiled directly into this headless test binary. This
  # gives future tests an independent view of the generated SPIR-V without
  # linking the renderer or creating a Vulkan device.
  ${CMAKE_SOURCE_DIR}/lib/spirv_reflect/spirv_reflect.c
)

target_compile_features(ren-reflection-tests PRIVATE cxx_std_20)
target_include_directories(ren-reflection-tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/lib
  ${slang_INCLUDE_DIRS}
)
target_link_libraries(ren-reflection-tests PRIVATE
  GTest::gtest_main
  fmt::fmt-header-only
  slang
)

# The application uses an install-relative RPATH, but this executable runs
# directly from the build tree and is never installed.
set_target_properties(ren-reflection-tests PROPERTIES
  BUILD_WITH_INSTALL_RPATH FALSE
  BUILD_RPATH "${slang_LIBRARY_DIR}"
)

include(GoogleTest)
gtest_discover_tests(ren-reflection-tests
  DISCOVERY_MODE PRE_TEST
  TEST_PREFIX "ren."
)

add_dependencies(ren-tests ren-reflection-tests)
