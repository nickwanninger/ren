cmake_minimum_required(VERSION 3.20)

# Detect system architecture and OS
set(ARCH_DIR "")

if(APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(ARCH_DIR "macos-aarch64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64")
    set(ARCH_DIR "macos-x86_64")
  endif()
elseif(UNIX AND NOT APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64")
    set(ARCH_DIR "linux-x86_64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    set(ARCH_DIR "linux-aarch64")
  endif()
elseif(WIN32)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64")
    set(ARCH_DIR "windows-x86_64")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    set(ARCH_DIR "windows-arm64")
  endif()
endif()

if(NOT ARCH_DIR)
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR} on ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Slang: Using architecture directory: ${ARCH_DIR}")

set(SLANG_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
set(SLANG_ROOT_DIR "${SLANG_BIN_DIR}/${ARCH_DIR}")
set(SLANG_INCLUDE_DIR "${SLANG_ROOT_DIR}/include")
set(SLANG_LIB_DIR "${SLANG_ROOT_DIR}/lib")
set(SLANG_CMAKE_DIR "${SLANG_LIB_DIR}/cmake/slang")


set(SLANG_VERSION "2025.22.1")

# Download Slang binaries if not present
if(NOT EXISTS "${SLANG_ROOT_DIR}")
  message(STATUS "Slang binaries not found, downloading for ${ARCH_DIR}...")

  set(SLANG_TAR "slang-${SLANG_VERSION}-${ARCH_DIR}.tar.gz")
  set(SLANG_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_TAR}")
  set(SLANG_TAR_PATH "${SLANG_BIN_DIR}/${SLANG_TAR}")

  # Download the tarball
  message(STATUS "Downloading from ${SLANG_URL}")
  file(DOWNLOAD "${SLANG_URL}" "${SLANG_TAR_PATH}" SHOW_PROGRESS)

  # Extract to bin/ARCH_DIR
  message(STATUS "Extracting to ${SLANG_ROOT_DIR}")
  file(ARCHIVE_EXTRACT INPUT "${SLANG_TAR_PATH}" DESTINATION "${SLANG_ROOT_DIR}"
    PATTERNS "*" VERBOSE)

  # Clean up tarball and share directory
  file(REMOVE "${SLANG_TAR_PATH}")
  file(REMOVE_RECURSE "${SLANG_ROOT_DIR}/share")

  message(STATUS "Slang downloaded and extracted successfully")
endif()

if(NOT EXISTS "${SLANG_ROOT_DIR}")
  message(FATAL_ERROR "Slang binaries not found at: ${SLANG_ROOT_DIR}")
endif()

# Use Slang's provided CMake config
list(APPEND CMAKE_PREFIX_PATH "${SLANG_CMAKE_DIR}")
find_package(slang REQUIRED)

# Create an alias target for easier use
add_library(slang INTERFACE)
target_link_libraries(slang INTERFACE slang::slang)
target_include_directories(slang INTERFACE ${SLANG_INCLUDE_DIR})
# Install the libslang-compiler.so.XXXX
install(FILES "${SLANG_LIB_DIR}/libslang-compiler.so.0.${SLANG_VERSION}" DESTINATION lib)


# Export variables and target for use in parent project
set(slang_INCLUDE_DIRS "${SLANG_INCLUDE_DIR}" PARENT_SCOPE)
set(slang_FOUND TRUE PARENT_SCOPE)

message(STATUS "Slang found: ${slang_FOUND}")