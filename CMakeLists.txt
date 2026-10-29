cmake_minimum_required(VERSION 3.20)
project(sfiii-ps5 CXX)
set(CMAKE_CXX_STANDARD 17)
# Dev tools: the network debug server is compiled only when ON. Release/store builds keep it OFF;
# the release check (tools/check-release.sh) refuses an ELF with devtools_ symbols.
option(SF3_PS5_DEVTOOLS "Build the network debug server (dev builds only)" OFF)

add_library(sf3ps5_crash STATIC src/devtools/crash_handler.cpp)
if(SF3_PS5_DEVTOOLS)
  add_library(sf3ps5_devtools STATIC src/devtools/debug_server.cpp)
  target_compile_definitions(sf3ps5_devtools PUBLIC SF3_PS5_DEVTOOLS=1)
  if(NOT CMAKE_CROSSCOMPILING)
    add_executable(test_debug_server src/devtools/test_debug_server.cpp)
    target_link_libraries(test_debug_server sf3ps5_devtools)
  endif()
endif()

# Vulkan scene pass (replaces sfiii-native's SDL_GPU render state). Desktop Vulkan for the tests;
# the PS5 build links PS5_Vulkan's RADV instead (see docs/vulkan.md).
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  add_library(sf3ps5_vulkan STATIC src/vulkan/vk_scene.cpp)
  target_link_libraries(sf3ps5_vulkan PUBLIC Vulkan::Vulkan)
  target_compile_options(sf3ps5_vulkan PRIVATE -Wno-missing-field-initializers)
  if(NOT CMAKE_CROSSCOMPILING)
    add_executable(vk_scene_test tests/vk_scene_test.cpp)
    target_link_libraries(vk_scene_test sf3ps5_vulkan)
  endif()
endif()

# The runtime side of the dev tools (src/devtools/runtime_agent.cpp) replaces saturn-recomp's
# machine/agent.cpp. SF3_PS5_RUNTIME_DIR is a patched saturn-recomp checkout (sfiii-native's
# tools/runtime-diagnostics.patch applied). This object library compiles it against the real runtime headers,
# so a header change shows up here before the console build.
set(SF3_PS5_RUNTIME_DIR "" CACHE PATH "Patched saturn-recomp checkout (optional, for the runtime_agent compile check)")
if(SF3_PS5_RUNTIME_DIR)
  add_library(sf3ps5_runtime_agent OBJECT src/devtools/runtime_agent.cpp)
  target_include_directories(sf3ps5_runtime_agent PRIVATE
    ${SF3_PS5_RUNTIME_DIR}/runtime/include ${SF3_PS5_RUNTIME_DIR}/runtime/include/saturn
    ${SF3_PS5_RUNTIME_DIR}/runtime/src/machine ${SF3_PS5_RUNTIME_DIR}/runtime/src/video
    ${SF3_PS5_RUNTIME_DIR}/runtime/src ${CMAKE_CURRENT_SOURCE_DIR}/src/devtools)
  if(SF3_PS5_DEVTOOLS)
    target_compile_definitions(sf3ps5_runtime_agent PRIVATE SF3_PS5_DEVTOOLS=1)
  endif()
endif()
