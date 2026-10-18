collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(renctl ${SOURCE_FILES})

install(TARGETS renctl
  RUNTIME DESTINATION bin
)
target_link_libraries(renctl PUBLIC
      ren
      slang
      )