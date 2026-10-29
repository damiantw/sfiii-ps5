cmake_minimum_required(VERSION 3.20)
project(sfiii-ps5 C CXX)
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

# Disc layer and on-console translation patcher (src/disc, src/patch). CHD comes from libchdr
# (BSD-3-Clause) with its bundled LZMA SDK (public domain), miniz (MIT) and zstd (BSD; dual
# BSD/GPLv2, taken under BSD) and dr_flac (public domain / MIT-0). See docs/third-party.md.
set(CHDR_WANT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WITH_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
set(WITH_SYSTEM_ZSTD OFF CACHE BOOL "" FORCE)
add_subdirectory(external/libchdr EXCLUDE_FROM_ALL)
add_library(sf3ps5_disc STATIC
  src/disc/sha.cpp src/disc/cdimage.cpp src/disc/identify.cpp src/disc/scan.cpp
  src/patch/dotnet.cpp src/patch/ipbin.cpp)
target_include_directories(sf3ps5_disc PUBLIC src)
target_link_libraries(sf3ps5_disc PUBLIC chdr-static)
# The translation patcher (port of sfiii-native tools/sf3patch); zip reading uses libchdr's miniz (MIT).
add_library(sf3ps5_patch STATIC
  src/patch/netjson.cpp src/patch/dfr.cpp src/patch/vfs.cpp src/patch/package.cpp src/patch/build.cpp src/patch/fetch.cpp)
target_link_libraries(sf3ps5_patch PUBLIC sf3ps5_disc miniz)
target_include_directories(sf3ps5_patch PRIVATE external/libchdr/deps/miniz-3.1.2)
if(NOT CMAKE_CROSSCOMPILING)
  add_executable(sf3disc src/cli/sf3disc.cpp)
  target_link_libraries(sf3disc sf3ps5_disc)
  add_executable(sf3patch src/cli/sf3patch.cpp)
  target_link_libraries(sf3patch sf3ps5_patch)
  add_executable(patch_quirks tests/patch_quirks.cpp)
  target_link_libraries(patch_quirks sf3ps5_patch)
endif()
