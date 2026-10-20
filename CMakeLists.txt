cmake_minimum_required(VERSION 3.20)

project(ren CXX C)

include(FetchContent)
include(CTest)


message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "CXX Compiler: ${CMAKE_CXX_COMPILER}")

if (WIN32)
  set(BUILD_SHARED_LIBS OFF)
endif()


# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib)
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin)


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
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan include dirs: ${Vulkan_INCLUDE_DIRS}")
include_directories(${Vulkan_INCLUDE_DIRS})

# We depend on LuaJIT as a scripting engine.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LuaJIT REQUIRED IMPORTED_TARGET luajit)
include_directories(${LuaJIT_INCLUDE_DIRS})
message(STATUS "LuaJIT include dirs: ${LuaJIT_INCLUDE_DIRS}")

# # SDL as a display and input handling library
find_package(SDL3 REQUIRED CONFIG)


# get_target_property(SDL3_INCLUDES SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
# message(STATUS "SDL3 includes: ${SDL3_INCLUDES}")
# include_directories(AFTER ${SDL3_INCLUDES})



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
# Static dependencies are linked into the shared ren library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Resolve zlib before Assimp's finder starts preferring static system archives,
# which may not have been compiled with PIC.
find_package(ZLIB REQUIRED)
set(ZLIB_LIBRARIES ZLIB::ZLIB)

# Disable the assimp formats we don't want.
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)
set(ASSIMP_BUILD_FBX_IMPORTER ON)
set(ASSIMP_BUILD_PLY_IMPORTER ON)
set(ASSIMP_BUILD_OBJ_IMPORTER ON)

# Don't install assimp, we just want to link against it statically.
set(ASSIMP_INSTALL OFF)
add_subdirectory(lib/assimp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Enable the large-file API used by Assimp's ZIP implementation.
  set_property(SOURCE lib/assimp/contrib/zip/src/zip.c TARGET_DIRECTORY assimp
    APPEND PROPERTY COMPILE_DEFINITIONS _LARGEFILE64_SOURCE)
endif()

# if on mac...
if(APPLE)
  # add -Wno-unused-const-variable to assimp. I think the compiler is new enough
  target_compile_options(assimp PRIVATE -Wno-unused-const-variable)
endif(APPLE)
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


# Set RPATH for all executables to find shared libraries relative to executable location.
# This must be set here (not in src/) so that apps/ also gets it.
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
if(APPLE)
  list(APPEND CMAKE_INSTALL_RPATH "@loader_path/../lib/")
elseif(UNIX)
  list(APPEND CMAKE_INSTALL_RPATH "$ORIGIN/../lib/")
endif()


add_subdirectory(src)
add_subdirectory(apps)
