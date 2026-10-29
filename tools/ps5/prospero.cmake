# CMake toolchain for the console build (tools/ps5/build-title.sh). The compilers and lld come from
# PS5_PAYLOAD_SDK (used, not shipped); the link uses this repo's MIT startup code, linker script and
# platform shim (ps5/platform), never the SDK's crt1.o, libc.a or linker scripts. MIT, see LICENSE.
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(NOT DEFINED ENV{PS5_PAYLOAD_SDK})
  message(FATAL_ERROR "set PS5_PAYLOAD_SDK")
endif()
set(PS5_SDK $ENV{PS5_PAYLOAD_SDK})
set(CMAKE_C_COMPILER ${PS5_SDK}/bin/prospero-clang)
set(CMAKE_CXX_COMPILER ${PS5_SDK}/bin/prospero-clang++)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH ${PS5_SDK}/target)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
get_filename_component(_sf3ps5 ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
set(_flags "-DSF3_PS5_CONSOLE=1 -ffunction-sections -fdata-sections -include ${_sf3ps5}/ps5/platform/compat_pre.h")
set(CMAKE_C_FLAGS_INIT "${_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_flags}")
# pthreads are in libkernel: nothing to add
set(CMAKE_THREAD_LIBS_INIT "")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_PTHREADS_INIT 1)
set(Threads_FOUND TRUE)
set(THREADS_PREFER_PTHREAD_FLAG OFF)
# The final link: prospero-lld with ps5/platform/ps5-title.ld; SF3_PS5_LINK_TAIL (set by
# tools/ps5/saturn_ps5.cmake) adds RADV, libc++ and the system-library stubs.
set(CMAKE_CXX_LINK_EXECUTABLE
  "${PS5_SDK}/bin/prospero-lld -T ${_sf3ps5}/ps5/platform/ps5-title.ld --eh-frame-hdr --undefined-version --wrap=pthread_create --version-script ${_sf3ps5}/ps5/platform/title-exports.map --exclude-libs=ALL --gc-sections -e _start -o <TARGET> <OBJECTS> <LINK_LIBRARIES> <LINK_FLAGS>")
