cmake_minimum_required(VERSION 3.20)

project(ren CXX C)

include(FetchContent)


message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER}")

if (WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")


# Set C standard
set(CMAKE_C_STANDARD_REQUIRED ON)

# set(CMAKE_BUILD_WITH_INSTALL_RPATH ON) # something with assimp
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # Enable compiler extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# ------- Find various packages we need -------
# We depend on LuaJIT as a scripting engine.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LuaJIT REQUIRED IMPORTED_TARGET luajit)
include_directories(${LuaJIT_INCLUDE_DIRS})

# SDL as a display and input handling library
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

# Vulkan as a Graphics API
find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
include_directories(${Vulkan_INCLUDE_DIRS})

# Add some of the libraries we vendor
add_subdirectory(lib/VulkanMemoryAllocator)
# add_subdirectory(lib/flecs)
add_subdirectory(lib/fmt)


# Slang shader compiler - using distributed binaries
add_subdirectory(lib/slang)

# configure CXX flags on gcc or msvc differently
if(MSVC)
    # I have no idea what to put here yet.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
    # add -O3 in release
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function -Wno-unused-const-variable ")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-deprecated-volatile -Wno-unused-function")
endif()


# Disable assimp tests, we don't need them, gonna trust it works.
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_BUILD_STATIC_LIB ON)

# Disable the assimp formats we don't want.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)
set(ASSIMP_BUILD_FBX_IMPORTER ON)
set(ASSIMP_BUILD_PLY_IMPORTER ON)
set(ASSIMP_BUILD_OBJ_IMPORTER ON)
add_subdirectory(lib/assimp)


# if on mac...
if(APPLE)
  # add -Wno-unused-const-variable to assimp. I think the compiler is new enough
  target_compile_options(assimp PRIVATE -Wno-unused-const-variable)
endif(APPLE)



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


add_subdirectory(src)

