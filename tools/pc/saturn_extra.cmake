# PC build of the PS5 app (src/host/host_runtime.cpp) against a generated game tree: pass to the
# generated CMakeLists.txt as -DSATURN_EXTRA=<this file> -DSF3_PS5_ROOT=<sfiii-ps5 checkout>.
# Links the runtime's machine with our host instead of its SDL3 host. The result is game-derived:
# keep its build folder outside the repo. See docs/pc-run.md.
if(NOT SF3_PS5_ROOT)
  message(FATAL_ERROR "SF3_PS5_ROOT: the sfiii-ps5 checkout")
endif()
get_filename_component(_rtroot ${SATURN_RUNTIME} DIRECTORY)
set(SF3_PS5_RUNTIME_DIR ${_rtroot} CACHE PATH "" FORCE)
add_subdirectory(${SF3_PS5_ROOT} sfiii-ps5 EXCLUDE_FROM_ALL)
add_executable(sf3ps5 ${SF3_PS5_ROOT}/src/host/host_runtime.cpp ${SF3_PS5_ROOT}/src/host/script.cpp
    ${SF3_PS5_ROOT}/src/host/window_screen.cpp ${SATURN_RUNTIME}/src/host/settings.cpp)
target_include_directories(sf3ps5 PRIVATE ${SF3_PS5_ROOT}/src ${SATURN_RUNTIME}/include ${SATURN_RUNTIME}/include/saturn
    ${SATURN_RUNTIME}/src/machine ${SATURN_RUNTIME}/src/video ${SATURN_RUNTIME}/src)
target_link_libraries(sf3ps5 saturn_core saturn_machine sf3ps5_host sf3ps5_prepare sf3ps5_runtime_bind recomp Threads::Threads)
# the shipped revision profiles, for a disc's first launch (src/prepare)
target_compile_definitions(sf3ps5 PRIVATE SF3_PS5_PROFILES_DIR="${SF3_PS5_ROOT}/external/sfiii-native/profiles")
# sfiii-native's SH-2 interpreter fallback, when the tree has it (tools/ps5/x64-profile.sh)
if(TARGET sf3_interp)
  target_sources(sf3ps5 PRIVATE $<TARGET_OBJECTS:sf3_interp>)
endif()
# the generic build's module set loader (sfiii-native native/recomp/generic)
if(TARGET sf3_x64_set)
  target_sources(sf3ps5 PRIVATE $<TARGET_OBJECTS:sf3_x64_set>)
endif()
