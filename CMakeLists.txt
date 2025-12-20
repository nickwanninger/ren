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
set(CMAKE_INSTALL_MESSAGE LAZY)

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON) # something with assimp
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON) # Enable compiler extensions
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# ------- Find various packages we need -------

# Vulkan as a Graphics API
message(STATUS "Vulkan SDK Set to: ${VULKAN_SDK}")
find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
include_directories(${Vulkan_INCLUDE_DIRS})

# We depend on LuaJIT as a scripting engine.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LuaJIT REQUIRED IMPORTED_TARGET luajit)
include_directories(${LuaJIT_INCLUDE_DIRS})

# SDL as a display and input handling library
find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})




# don't install libfmt
set(FMT_INSTALL OFF)
add_subdirectory(lib/fmt)


# Slang shader compiler - using distributed binaries
add_subdirectory(lib/slang)

set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# Disable assimp tests, we don't need them, gonna trust it works.
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_BUILD_STATIC_LIB ON)
set(BUILD_SHARED_LIBS OFF)

# Disable the assimp formats we don't want.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)
set(ASSIMP_BUILD_FBX_IMPORTER ON)
set(ASSIMP_BUILD_PLY_IMPORTER ON)
set(ASSIMP_BUILD_OBJ_IMPORTER ON)

# Don't install assimp, we just want to link against it statically.
set(ASSIMP_INSTALL OFF)
add_subdirectory(lib/assimp)

# if on mac...
if(APPLE)
  # add -Wno-unused-const-variable to assimp. I think the compiler is new enough
  target_compile_options(assimp PRIVATE -Wno-unused-const-variable)
endif(APPLE)



add_subdirectory(src)
