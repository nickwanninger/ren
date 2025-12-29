




collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")


set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/lib/imgui)
set(IMNODES_DIR ${CMAKE_SOURCE_DIR}/lib/imnodes)
set(IMGUI_SOURCES
   ${IMGUI_DIR}/imgui.cpp
   ${IMGUI_DIR}/imgui_demo.cpp
   ${IMGUI_DIR}/imgui_draw.cpp
   ${IMGUI_DIR}/imgui_tables.cpp
   ${IMGUI_DIR}/imgui_widgets.cpp

    ${IMNODES_DIR}/imnodes.cpp
   
   ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
   ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)


add_library(ren SHARED
  ${SOURCE_FILES}

  ${CMAKE_SOURCE_DIR}/lib/spirv_reflect/spirv_reflect.c
  ${CMAKE_SOURCE_DIR}/lib/spirv_reflect/common.cpp
  ${CMAKE_SOURCE_DIR}/lib/spirv_reflect/output_stream.cpp
  ${CMAKE_SOURCE_DIR}/lib/vkb/VkBootstrap.cpp
  ${CMAKE_SOURCE_DIR}/lib/stb/stb_image.c


  # Imguizmo
  ${CMAKE_SOURCE_DIR}/lib/ImGuizmo/GraphEditor.cpp
  ${CMAKE_SOURCE_DIR}/lib/ImGuizmo/ImCurveEdit.cpp
  ${CMAKE_SOURCE_DIR}/lib/ImGuizmo/ImGradient.cpp
  ${CMAKE_SOURCE_DIR}/lib/ImGuizmo/ImGuizmo.cpp
  ${CMAKE_SOURCE_DIR}/lib/ImGuizmo/ImSequencer.cpp



  ${CMAKE_SOURCE_DIR}/lib/flecs/flecs.c


  ${IMGUI_SOURCES}
)


install(TARGETS ren 
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin
)

target_compile_features(ren
  PRIVATE
  cxx_std_20
)


target_include_directories(ren BEFORE PRIVATE ${slang_INCLUDE_DIRS})
message(STATUS "Slang include dirs: ${slang_INCLUDE_DIRS}")



# Link libraries
target_link_libraries(ren PUBLIC

  slang
  SDL3::SDL3
  SDL3::Headers
  fmt::fmt-header-only # fmt library
  PkgConfig::LuaJIT
  assimp
  Vulkan::Vulkan # Vulkan SDK
  Vulkan::shaderc_combined
)

# if not on windows, link libm
if(NOT WIN32)
  target_link_libraries(ren PRIVATE m)
endif()

if (WIN32)

else()
  # Compiler flags
  target_compile_options(ren PRIVATE 
      ${SDL3_CFLAGS_OTHER}
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


# print the include dirs for ren
get_target_property(REN_INCLUDE_DIRS ren INCLUDE_DIRECTORIES)
message(STATUS "ren include dirs: ${REN_INCLUDE_DIRS}")
# Print the global include directories
message(STATUS "Global include dirs: ${CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE}")