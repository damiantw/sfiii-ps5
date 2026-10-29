# The console app (ELF, before ps5-native-tool): sfiii-native's generic build (no disc-derived
# code) with this repo's host. Pass to native/recomp/generic as -DSATURN_EXTRA=<this file>,
# with -DCMAKE_TOOLCHAIN_FILE=tools/ps5/prospero.cmake; tools/ps5/build-title.sh does it all.
# MIT, see LICENSE.
if(NOT SF3_PS5_ROOT OR NOT SF3_PS5_TITLE_ID OR NOT RADV_ARCHIVE OR NOT SF3_PS5_STUBS)
  message(FATAL_ERROR "SF3_PS5_ROOT, SF3_PS5_TITLE_ID, RADV_ARCHIVE and SF3_PS5_STUBS are required")
endif()
get_filename_component(_rtroot ${SATURN_RUNTIME} DIRECTORY)
set(SF3_PS5_RUNTIME_DIR ${_rtroot} CACHE PATH "" FORCE)
set(SF3_PS5_CONSOLE ON CACHE BOOL "" FORCE)
add_subdirectory(${SF3_PS5_ROOT} sfiii-ps5 EXCLUDE_FROM_ALL)
# the console's code-cache loader instead of sfiii-native's POSIX one
get_target_property(_rs recomp SOURCES)
list(FILTER _rs EXCLUDE REGEX "loader_posix\\.cpp$")
list(APPEND _rs ${SF3_PS5_ROOT}/src/ps5/loader_ps5.cpp)
set_property(TARGET recomp PROPERTY SOURCES ${_rs})
set(_p ${SF3_PS5_ROOT}/ps5/platform)
add_executable(sf3ps5 ${SF3_PS5_ROOT}/src/host/host_runtime.cpp ${SATURN_RUNTIME}/src/host/settings.cpp
    ${_p}/crt/start.c ${_p}/crt/cxx_new.cpp ${_p}/title/fpenv.c ${_p}/title/libc_gaps.c ${_p}/title/thread_stack.c ${_p}/title/locale_gaps.c ${_p}/title/path_shims.c
    ${_p}/vk_dispatch.cpp $<TARGET_OBJECTS:sf3_interp> $<TARGET_OBJECTS:sf3_x64_set>)
target_include_directories(sf3ps5 PRIVATE ${SF3_PS5_ROOT}/src ${_p} ${SATURN_RUNTIME}/include ${SATURN_RUNTIME}/include/saturn
    ${SATURN_RUNTIME}/src/machine ${SATURN_RUNTIME}/src/video ${SATURN_RUNTIME}/src)
target_compile_definitions(sf3ps5 PRIVATE SF3_PS5_PROFILES_DIR="/app0/profiles")
target_compile_definitions(sf3ps5_host PRIVATE SF3_PS5_TITLE_ID="${SF3_PS5_TITLE_ID}")
# Dev build (SF3_PS5_DEVTOOLS=ON): the debug server, in the launcher and (runtime_agent.cpp,
# in place of the runtime's machine/agent.cpp) in game. Enabled at run time by /data/sfiii/devtools.enable.
if(SF3_PS5_DEVTOOLS)
  get_target_property(_ms saturn_machine SOURCES)
  list(FILTER _ms EXCLUDE REGEX "machine/agent\\.cpp$")
  set_property(TARGET saturn_machine PROPERTY SOURCES ${_ms})
  target_sources(sf3ps5 PRIVATE ${SF3_PS5_ROOT}/src/devtools/runtime_agent.cpp)
  target_compile_definitions(sf3ps5 PRIVATE SF3_PS5_DEVTOOLS=1)
  target_link_libraries(sf3ps5 sf3ps5_devtools)
endif()
target_link_libraries(sf3ps5 saturn_core saturn_machine sf3ps5_host sf3ps5_prepare sf3ps5_runtime_bind recomp)
set(_l ${PS5_SDK}/target/lib)
file(GLOB _sce ${_l}/*.so)
execute_process(COMMAND clang --print-resource-dir OUTPUT_VARIABLE _res OUTPUT_STRIP_TRAILING_WHITESPACE)
string(JOIN " " _scel ${_sce})
set_property(TARGET sf3ps5 PROPERTY LINK_FLAGS
  "${SF3_PS5_STUBS}/libSceAgc.so ${SF3_PS5_STUBS}/libSceAgcDriver.so -L${_l} --whole-archive ${RADV_ARCHIVE} --no-whole-archive --start-group ${_l}/libc++.a ${_l}/libc++abi.a ${_l}/libunwind.a ${_res}/lib/linux/libclang_rt.builtins-x86_64.a --end-group --as-needed ${_scel}")
