
find_package(Git)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY "${local_dir}"
    OUTPUT_VARIABLE _build_version
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  add_compile_definitions(-DREN_GIT_REVISION="${_build_version}")
else()
  add_compile_definitions(-DREN_GIT_REVISION="UNK")
endif()
string(TIMESTAMP TODAY "%Y.%m.%d")
add_compile_definitions(-DREN_BUILD_DATE="${TODAY}")


add_subdirectory(ren)
add_subdirectory(editor)