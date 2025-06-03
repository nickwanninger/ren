cmake_minimum_required(VERSION 3.16)

project(ren 
    VERSION 1.0.0
    LANGUAGES CXX)

# Set C standard
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find OpenMP (optional)
find_package(OpenMP)
if(OpenMP_C_FOUND)
  target_link_libraries(ren OpenMP::OpenMP_C)
  message(STATUS "OpenMP found - parallel processing enabled")
else()
  message(STATUS "OpenMP not found - using single-threaded execution")
endif()


# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Add executable
add_executable(ren
  src/main.cpp
  src/RenderTarget.cpp
)

# Link libraries
target_link_libraries(ren 
  ${SDL2_LIBRARIES}
  m  # math library for sin/cos functions
)

# Include directories
target_include_directories(ren PRIVATE 
  ${SDL2_INCLUDE_DIRS}
  include/
)

# Compiler flags
target_compile_options(ren PRIVATE 
    ${SDL2_CFLAGS_OTHER}
    -Wall
    -Wextra
    -O3
)
