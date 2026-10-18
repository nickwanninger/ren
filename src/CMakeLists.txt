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


add_subdirectory(ren)
add_subdirectory(editor)
add_subdirectory(renctl)
add_subdirectory(bundler)
