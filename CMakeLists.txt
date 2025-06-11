cmake_minimum_required(VERSION 3.6)

project(ren CXX C)

# set(CMAKE_C_COMPILER gcc)
# set(CMAKE_CXX_COMPILER g++)

# Set C standard
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find OpenMP (optional)


# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

find_package(Vulkan REQUIRED)
find_package(fmt REQUIRED)
find_package(OpenMP)

# Add executable
add_executable(ren
  src/main.cpp
  src/RenderTarget.cpp

  src/Engine.cpp
  src/VulkanInstance.cpp

  # src/wren/optional/wren_opt_meta.c
  # src/wren/optional/wren_opt_random.c
  # src/wren/vm/wren_compiler.c
  # src/wren/vm/wren_core.c
  # src/wren/vm/wren_debug.c
  # src/wren/vm/wren_primitive.c
  # src/wren/vm/wren_utils.c
  # src/wren/vm/wren_value.c
  # src/wren/vm/wren_vm.c
)



if(OpenMP_C_FOUND)
  target_link_libraries(ren PUBLIC OpenMP::OpenMP_C)
  message(STATUS "OpenMP found - parallel processing enabled")
else()
  message(STATUS "OpenMP not found - using single-threaded execution")
endif()

find_program(GLSLANG_VALIDATOR glslang HINTS 
    ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
    /usr/bin
    /usr/local/bin
    ${VULKAN_SDK_PATH}/Bin
    ${VULKAN_SDK_PATH}/Bin32
    $ENV{VULKAN_SDK}/Bin/ 
    $ENV{VULKAN_SDK}/Bin32/
)


# Function to compile shaders
function(add_shader TARGET SHADER)
    set(current-shader-path ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER})
    set(current-output-path ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER}.spv)

    get_filename_component(current-output-dir ${current-output-path} DIRECTORY)
    file(MAKE_DIRECTORY ${current-output-dir})

    add_custom_command(
        OUTPUT ${current-output-path}
        COMMAND ${GLSLANG_VALIDATOR} -V ${current-shader-path} -o ${current-output-path}
        DEPENDS ${current-shader-path}
        IMPLICIT_DEPENDS CXX ${current-shader-path}
        VERBATIM
    )

    set_source_files_properties(${current-output-path} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${current-output-path})
endfunction(add_shader)

# Find all shader files
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.geom"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.tesc"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.tese"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "Adding shader: ${GLSL}")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  add_shader(ren "shaders/${FILE_NAME}")
endforeach(GLSL)




# Link libraries
target_link_libraries(ren
  PUBLIC
  ${SDL2_LIBRARIES}
  # OpenGL::GL
  # OpenGL::GLU
  glfw
  fmt::fmt

  Vulkan::Vulkan
  m  # math library for sin/cos functions
)


# Include directories
target_include_directories(ren PRIVATE
  ${SDL2_INCLUDE_DIRS}
  # include/ has many vendored libraries (GLM, mainly)
  include/
  # src/wren/vm/
  # src/wren/optional/
)

# Compiler flags
target_compile_options(ren PRIVATE 
    ${SDL2_CFLAGS_OTHER}
    -Wall
    # -Wextra
    -Wno-deprecated-declarations
    -Wno-unused-parameter
    -O3
    -g3
)
