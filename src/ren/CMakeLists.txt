




collect_sources(SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}")


set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/lib/imgui)
set(IMGUI_SOURCES
   ${IMGUI_DIR}/imgui.cpp
   ${IMGUI_DIR}/imgui_demo.cpp
   ${IMGUI_DIR}/imgui_draw.cpp
   ${IMGUI_DIR}/imgui_tables.cpp
   ${IMGUI_DIR}/imgui_widgets.cpp
   
   ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
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
  ${CMAKE_SOURCE_DIR}/lib/tracy/TracyClient.cpp



  ${CMAKE_SOURCE_DIR}/lib/flecs/flecs.c


  ${IMGUI_SOURCES}
)


target_compile_features(ren
  PRIVATE
  cxx_std_20
)

# target_include_directories(ren INTERFACE ${LuaJIT_INCLUDE_DIRS})

# Link libraries
target_link_libraries(ren
  PUBLIC
  ${SDL2_LIBRARIES}
  fmt::fmt-header-only # fmt library

  PkgConfig::LuaJIT

  assimp

  Vulkan::Vulkan # Vulkan SDK
  GPUOpen::VulkanMemoryAllocator # Vulkan Memory Allocator
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

