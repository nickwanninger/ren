collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(rentest ${SOURCE_FILES})

# Tests in tests/ include the harness headers by name, e.g. "RenderTest.h".
target_include_directories(rentest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS rentest
  RUNTIME DESTINATION bin
)
target_link_libraries(rentest PUBLIC
      ren
      slang
      )
