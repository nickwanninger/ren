# configure CXX flags on gcc or msvc differently
if(MSVC)
    # I have no idea what to put here yet.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
    # add -O3 in release
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function -Wno-unused-const-variable ")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-deprecated-volatile -Wno-unused-function -Wno-deprecated-literal-operator")
endif()


include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui) # hack to include imgui headers properly
include_directories(${CMAKE_SOURCE_DIR}/lib/imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/lib/fmt/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# ---------------------- Embedded Resources ---------------------- #

function(embed_file input_file output_file variable_name)
# only update if input_file is newer than output_file
    if(EXISTS ${output_file})
        file(TIMESTAMP ${input_file} input_time "%s")
        file(TIMESTAMP ${output_file} output_time "%s")
        if(input_time LESS output_time)
            return()
        endif()
    endif()


    # take input_file and remove everything before ${CMAKE_SOURCE_DIR}/src/ren/res/
    file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}/src/ren/res" ${input_file})

    # make sure the output directory exists
    get_filename_component(output_dir ${output_file} DIRECTORY)
    file(MAKE_DIRECTORY ${output_dir})
    set(temp_output "${output_file}.gz")
    file(ARCHIVE_CREATE 
        OUTPUT ${temp_output}
        PATHS ${input_file}
        FORMAT raw
        COMPRESSION_LEVEL 9
        COMPRESSION GZip
    )


    # Get the original file size in bytes
    file(SIZE ${input_file} original_size)
    file(SIZE ${temp_output} compressed_size)
    file(READ ${temp_output} hex_content HEX)

    # compute compression ratio
    message(STATUS "Embedding ${input_file} (${compressed_size} bytes, compressed)")
    
    # Split into byte array
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," byte_array ${hex_content})
    
    file(WRITE ${output_file}
        "static const unsigned char embed[] = {${byte_array}};\n"
        "static const unsigned long embed_size = ${compressed_size};\n"
        "static const unsigned long embed_original_size = ${original_size};\n"
        "extern void __ren_register_embedded_resource(const char *name, const unsigned char *data, unsigned long size, unsigned long original_size);\n"
        "__attribute__((constructor))\n"
        "static void __register() { __ren_register_embedded_resource(\"${rel_path}\", embed, embed_size, embed_original_size); }\n"
    )
endfunction()

function(collect_embedded_resources OUT_VAR GLOB)
  # initialize local list from parent's value (if any)
  if(DEFINED ${OUT_VAR})
    set(_out_list "${${OUT_VAR}}")
  else()
    set(_out_list)
  endif()

  file(GLOB_RECURSE _res_files "${GLOB}")
  foreach(_f IN LISTS _res_files)
    file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}/resources" "${_f}")
    string(MAKE_C_IDENTIFIER "${rel_path}" var_name)
    embed_file("${_f}" "${CMAKE_BINARY_DIR}/res/${var_name}.c" "embedded_${var_name}")
    list(APPEND _out_list "${CMAKE_BINARY_DIR}/res/${var_name}.c")
  endforeach()

  set(${OUT_VAR} "${_out_list}" PARENT_SCOPE)
endfunction()

function(collect_sources OUT_VAR DIR)
  set(SOURCE_FILES "")
  file(GLOB_RECURSE SOURCE_FILES "${DIR}/*.cpp" "${DIR}/*.c")
  collect_embedded_resources(EMBEDDED_SOURCES "${DIR}/res/*")
  set(${OUT_VAR} "${SOURCE_FILES};${EMBEDDED_SOURCES}" PARENT_SCOPE)
endfunction()


set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
# Set RPATH for all executables to find shared libraries relative to executable location
if(APPLE)
  list(APPEND CMAKE_INSTALL_RPATH "@loader_path/../lib/")
elseif(UNIX)
  list(APPEND CMAKE_INSTALL_RPATH "$ORIGIN/../lib/")
endif()

add_subdirectory(ren)
add_subdirectory(editor)
add_subdirectory(bundler)

# Keep the test suite optional for developer builds. We intentionally do not
# download GoogleTest here: packagers and developers can provide it through
# CMAKE_PREFIX_PATH or GTest_DIR.
add_custom_target(ren-tests)
if(BUILD_TESTING)
  find_package(GTest QUIET)
  if(GTest_FOUND)
    message(STATUS "GoogleTest found; enabling REN tests")
    add_subdirectory(test)
  else()
    message(STATUS "GoogleTest not found; REN tests are disabled")
  endif()
endif()
