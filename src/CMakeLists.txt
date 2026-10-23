# ---------------------- Embedded Resources ---------------------- #

function(embed_file input_file output_file res_root)
    # Only update if input_file or this script is newer than output_file.
    # A change to this script can change the generated code, so it also
    # makes the old output stale.
    if(NOT "${input_file}" IS_NEWER_THAN "${output_file}" AND
       NOT "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" IS_NEWER_THAN "${output_file}")
        return()
    endif()


    # The resource name is the path relative to the res/ directory it came from.
    file(RELATIVE_PATH rel_path "${res_root}" ${input_file})

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

function(collect_embedded_resources OUT_VAR RES_ROOT)
  # initialize local list from parent's value (if any)
  if(DEFINED ${OUT_VAR})
    set(_out_list "${${OUT_VAR}}")
  else()
    set(_out_list)
  endif()

  # CONFIGURE_DEPENDS re-runs the glob when files are added or removed.
  # CMAKE_CONFIGURE_DEPENDS re-runs the configure step when a resource is
  # edited, so the embedded copy does not go stale.
  file(GLOB_RECURSE _res_files CONFIGURE_DEPENDS "${RES_ROOT}/*")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_res_files})

  # Every target that links ren also gets the engine resources. Names share
  # one flat namespace, and the lookup order is the (undefined) order of the
  # static constructors, so a duplicate name is an error.
  set(REN_ENGINE_RES_ROOT "${CMAKE_SOURCE_DIR}/src/ren/res")
  if(NOT RES_ROOT STREQUAL REN_ENGINE_RES_ROOT)
    file(GLOB_RECURSE _engine_names RELATIVE "${REN_ENGINE_RES_ROOT}" "${REN_ENGINE_RES_ROOT}/*")
    foreach(_f IN LISTS _res_files)
      file(RELATIVE_PATH _name "${RES_ROOT}" "${_f}")
      if(_name IN_LIST _engine_names)
        message(FATAL_ERROR "Embedded resource '${_name}' in ${RES_ROOT} has the same name as an engine resource in ${REN_ENGINE_RES_ROOT}")
      endif()
    endforeach()
  endif()

  foreach(_f IN LISTS _res_files)
    file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}/resources" "${_f}")
    string(MAKE_C_IDENTIFIER "${rel_path}" var_name)
    embed_file("${_f}" "${CMAKE_BINARY_DIR}/res/${var_name}.c" "${RES_ROOT}")
    list(APPEND _out_list "${CMAKE_BINARY_DIR}/res/${var_name}.c")
  endforeach()

  set(${OUT_VAR} "${_out_list}" PARENT_SCOPE)
endfunction()

function(collect_sources OUT_VAR DIR)
  set(SOURCE_FILES "")
  file(GLOB_RECURSE SOURCE_FILES "${DIR}/*.cpp" "${DIR}/*.c")
  collect_embedded_resources(EMBEDDED_SOURCES "${DIR}/res")
  set(${OUT_VAR} "${SOURCE_FILES};${EMBEDDED_SOURCES}" PARENT_SCOPE)
endfunction()


add_subdirectory(ren)
add_subdirectory(editor)
add_subdirectory(renctl)
add_subdirectory(bundler)
