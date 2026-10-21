collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(rentest ${SOURCE_FILES})

# Tests in tests/ include the harness headers by name, e.g. "RenderTest.h".
target_include_directories(rentest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Golden images are in the source tree, so that --save-golden writes files
# that git tracks. The images of failed comparisons go to the build tree.
target_compile_definitions(rentest PRIVATE
  RENTEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
  RENTEST_OUTPUT_DIR="${CMAKE_BINARY_DIR}/rentest"
)

install(TARGETS rentest
  RUNTIME DESTINATION bin
)

# Run the installed binary from the source root, like the other tests, so
# that the rpath finds dist/lib and assets/ resolves.
if(BUILD_TESTING)
  add_test(NAME rentest
    COMMAND ${CMAKE_INSTALL_PREFIX}/bin/rentest
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
target_link_libraries(rentest PUBLIC
      ren
      slang
      )
