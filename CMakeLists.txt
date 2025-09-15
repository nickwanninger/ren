cmake_minimum_required(VERSION 3.20)

project(ren CXX C)

include(FetchContent)

if (WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")


# tell cmake to print the cmake path
message(STATUS "CMake path: ${CMAKE_CURRENT_SOURCE_DIR}")

# Set C standard
# set(CMAKE_C_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # Enable compiler extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find various packages we need
find_package(SDL2 REQUIRED)
find_package(Vulkan REQUIRED glslang shaderc_combined)
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")

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
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-function -Wno-deprecated-volatile -Wno-unused-function")
endif()

# Define a variable to store the list of C++ source files
set(REN_SOURCE_FILES "")

# Recursively find all .cpp files in the current directory and its subdirectories
file(GLOB_RECURSE REN_SOURCE_FILES "src/ren/*.cpp")

# Enable Tracy profiler
add_compile_definitions(TRACY_ENABLE)

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
  fmt::fmt # fmt library

  # flecs::flecs_static # Flecs for ECS
)

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


