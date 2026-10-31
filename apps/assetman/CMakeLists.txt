collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(assetman ${SOURCE_FILES})

install(TARGETS assetman
  RUNTIME DESTINATION bin
)
target_link_libraries(assetman PUBLIC
      ren
      slang
      )

if(BUILD_TESTING)
  # The executable uses the installed library path, like the other application tests.
  add_test(NAME assetman-authoring
    COMMAND ${CMAKE_INSTALL_PREFIX}/bin/assetman --test-authoring
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
