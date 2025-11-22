collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "Editor source files: ${SOURCE_FILES} ")
add_executable(editor ${SOURCE_FILES})

target_link_libraries(editor PUBLIC
      # $<LINK_LIBRARY:WHOLE_ARCHIVE:ren>
      ren
      slang
      )