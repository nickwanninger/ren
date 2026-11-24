collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(raytracer ${SOURCE_FILES})

install(TARGETS raytracer
  RUNTIME DESTINATION bin
)
target_link_libraries(raytracer PUBLIC
      ren
      slang
      )