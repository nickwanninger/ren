cmake_minimum_required(VERSION 3.6)

project(ren CXX C)

# Set C standard
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find various packages we need
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
find_package(Vulkan REQUIRED)
find_package(fmt REQUIRED)


# Add some of the libraries we vendor
add_subdirectory(lib/VulkanMemoryAllocator)
add_subdirectory(lib/entt)

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








# Ren is the main target of this project. For now, it's a single executable,
# but eventually ren should be `libren` and we can have multiple executables
# that link against it.
add_executable(ren
  src/main.cpp

  src/Engine.cpp
  src/renderer/VulkanInstance.cpp
  src/renderer/Buffer.cpp
  src/renderer/Shader.cpp
  src/renderer/VulkanPipeline.cpp

  lib/vkb/VkBootstrap.cpp
  lib/stb/stb_image.c

  ${IMGUI_SOURCES}
)



# Link libraries
target_link_libraries(ren
  PUBLIC
  ${SDL2_LIBRARIES}
  fmt::fmt # fmt library

  Vulkan::Vulkan # Vulkan SDK
  GPUOpen::VulkanMemoryAllocator # Vulkan Memory Allocator
  EnTT::EnTT # EnTT for ECS

  m # Everything needs math
)


# Include directories
target_include_directories(ren PRIVATE
  ${SDL2_INCLUDE_DIRS}
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends

  include/
  lib/
)

# Compiler flags
target_compile_options(ren PRIVATE 
    ${SDL2_CFLAGS_OTHER}
    -Wall
    -Wno-nullability-completeness
    -Wno-unused-variable
    -Wno-unused-private-field
    -Wno-deprecated-declarations
    -Wno-unused-parameter
    -O3
    -g3
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




