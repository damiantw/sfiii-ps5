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
