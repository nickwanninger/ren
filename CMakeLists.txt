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


# tell cmake to print the cmake path
message(STATUS "CMake path: ${CMAKE_CURRENT_SOURCE_DIR}")

# Set C standard
set(CMAKE_C_STANDARD_REQUIRED ON)

# set(CMAKE_BUILD_WITH_INSTALL_RPATH ON) # something with assimp
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # Enable compiler extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find various packages we need
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")

# grab luajit (required, static)
find_package(PkgConfig REQUIRED)  
pkg_check_modules(LuaJIT REQUIRED IMPORTED_TARGET luajit)

# Add some of the libraries we vendor
add_subdirectory(lib/VulkanMemoryAllocator)
# add_subdirectory(lib/flecs)
add_subdirectory(lib/fmt)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/imgui)
set(IMGUI_SOURCES
   ${IMGUI_DIR}/imgui.cpp
   ${IMGUI_DIR}/imgui_demo.cpp
   ${IMGUI_DIR}/imgui_draw.cpp
   ${IMGUI_DIR}/imgui_tables.cpp
   ${IMGUI_DIR}/imgui_widgets.cpp
   
   ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
   ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Disable assimp tests, we don't need them, gonna trust it works.
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_BUILD_STATIC_LIB ON)

# Disable the assimp formats we don't want.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)
set(ASSIMP_BUILD_FBX_IMPORTER ON)
set(ASSIMP_BUILD_PLY_IMPORTER ON)
add_subdirectory(lib/assimp)

# configure CXX flags on gcc or msvc differently
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
    # add -O3 in release
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-function ")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-deprecated-volatile -Wno-unused-function")
endif()






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

file(GLOB_RECURSE files "src/ren/res/*")
foreach(file ${files})
    file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}/resources" ${file})
    string(MAKE_C_IDENTIFIER ${rel_path} var_name)
    embed_file(${file} "${CMAKE_BINARY_DIR}/res/${var_name}.c" "embedded_${var_name}")
    list(APPEND EMBEDDED_SOURCES "${CMAKE_BINARY_DIR}/res/${var_name}.c")
endforeach()






# Define a variable to store the list of C++ source files
set(REN_SOURCE_FILES "")

# Recursively find all .cpp files in the current directory and its subdirectories
file(GLOB_RECURSE REN_SOURCE_FILES "src/ren/*.cpp" "src/ren/**/*.c")

# Enable Tracy profiler
# add_compile_definitions(TRACY_ENABLE)

# Ren is the main target of this project. For now, it's a single executable,
# but eventually ren should be `libren` and we can have multiple executables
# that link against it.
add_executable(ren

  ${REN_SOURCE_FILES}
  ${EMBEDDED_SOURCES}

  lib/spirv_reflect/spirv_reflect.c
  lib/spirv_reflect/common.cpp
  lib/spirv_reflect/output_stream.cpp
  lib/vkb/VkBootstrap.cpp
  lib/stb/stb_image.c


  # Imguizmo
  lib/ImGuizmo/GraphEditor.cpp
  lib/ImGuizmo/ImCurveEdit.cpp
  lib/ImGuizmo/ImGradient.cpp
  lib/ImGuizmo/ImGuizmo.cpp
  lib/ImGuizmo/ImSequencer.cpp

  lib/tracy/TracyClient.cpp



  lib/flecs/flecs.c


  ${IMGUI_SOURCES}
)

target_compile_features(ren
  PRIVATE
  cxx_std_20
)



# Link libraries
target_link_libraries(ren
  PUBLIC
  ${SDL2_LIBRARIES}
  fmt::fmt-header-only # fmt library

  PkgConfig::LuaJIT

  # flecs::flecs_static # Flecs for ECS
)

target_include_directories(ren PRIVATE ${LuaJIT_INCLUDE_DIRS})

# if not on windows, link libm
if(NOT WIN32)
  target_link_libraries(ren PRIVATE m)
endif()

target_link_libraries(ren
  PRIVATE
  assimp

  Vulkan::Vulkan # Vulkan SDK
  GPUOpen::VulkanMemoryAllocator # Vulkan Memory Allocator
  Vulkan::shaderc_combined
)

# Include directories
target_include_directories(ren PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends

  ${GUILE_STATIC_INCLUDE_DIRS}

  ${Vulkan_INCLUDE_DIRS}


  src/
  include/
  lib/
)

if (WIN32)

else()
  # Compiler flags
  target_compile_options(ren PRIVATE 
      ${SDL2_CFLAGS_OTHER}
      -Wall
      -Wno-nullability-completeness
      -Wno-unused-variable
      -Wno-unused-private-field
      -Wno-deprecated-declarations
      -Wno-unused-parameter
      # -O3
      -g3
  )
endif()


