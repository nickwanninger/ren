cmake_minimum_required(VERSION 3.20)

project(ren CXX C)

include(FetchContent)

# Set C standard
# set(CMAKE_C_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # Enable compiler extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# Find various packages we need
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
find_package(Vulkan REQUIRED glslang shaderc_combined)







# Add some of the libraries we vendor
add_subdirectory(lib/VulkanMemoryAllocator)
add_subdirectory(lib/flecs)
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

# Disable the assimp formats we don't want.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)
set(ASSIMP_BUILD_FBX_IMPORTER ON)
set(ASSIMP_BUILD_PLY_IMPORTER ON)
add_subdirectory(lib/assimp)



set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -std=c++20 -Wno-unused-function -Wno-deprecated-volatile ")

# Define a variable to store the list of C++ source files
set(REN_SOURCE_FILES "")

# Recursively find all .cpp files in the current directory and its subdirectories
file(GLOB_RECURSE REN_SOURCE_FILES "src/ren/*.cpp")

# Ren is the main target of this project. For now, it's a single executable,
# but eventually ren should be `libren` and we can have multiple executables
# that link against it.
add_executable(ren

  ${REN_SOURCE_FILES}

  lib/spirv_reflect/spirv_reflect.c
  lib/spirv_reflect/common.cpp
  lib/spirv_reflect/output_stream.cpp
  lib/vkb/VkBootstrap.cpp
  lib/stb/stb_image.c
  lib/tinyobjloader/tiny_obj_loader.cc
  lib/tinygltf/tiny_gltf.cc


  # Imguizmo
  lib/ImGuizmo/GraphEditor.cpp
  lib/ImGuizmo/ImCurveEdit.cpp
  lib/ImGuizmo/ImGradient.cpp
  lib/ImGuizmo/ImGuizmo.cpp
  lib/ImGuizmo/ImSequencer.cpp


  ${IMGUI_SOURCES}
)



# Link libraries
target_link_libraries(ren
  PUBLIC
  ${SDL2_LIBRARIES}
  fmt::fmt # fmt library

  flecs::flecs_static # Flecs for ECS
  m # Everything needs math
)

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


  src/
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
    # -O3
    -g3
)
