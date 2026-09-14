# Supply installed host tools, CMAKE_SYSROOT and sysroot-relative kernel headers.
# This cache builds Linux libc components, not an installed complete runtime.
if(NOT IS_ABSOLUTE "${CMAKE_SYSROOT}" OR
   NOT IS_DIRECTORY "${CMAKE_SYSROOT}" OR CMAKE_SYSROOT STREQUAL "/")
  message(FATAL_ERROR "MMIX Linux libc requires an explicit non-root CMAKE_SYSROOT")
endif()
get_filename_component(_mmix_sysroot "${CMAKE_SYSROOT}" REALPATH)
if(_mmix_sysroot STREQUAL "/")
  message(FATAL_ERROR "MMIX Linux libc requires an explicit non-root CMAKE_SYSROOT")
endif()

# Cross builds emit -idirafter=<path>, where '=' denotes the compiler sysroot.
if(NOT IS_ABSOLUTE "${LIBC_KERNEL_HEADERS}" OR
   LIBC_KERNEL_HEADERS MATCHES "(^|/)\\.\\.(/|$)" OR
   LIBC_KERNEL_HEADERS MATCHES ";")
  message(FATAL_ERROR "MMIX Linux libc requires a sysroot-relative LIBC_KERNEL_HEADERS path")
endif()
get_filename_component(_mmix_kernel_headers
  "${CMAKE_SYSROOT}${LIBC_KERNEL_HEADERS}" REALPATH)
string(FIND "${_mmix_kernel_headers}/" "${_mmix_sysroot}/" _mmix_header_prefix)
if(NOT _mmix_header_prefix EQUAL 0 OR
   NOT EXISTS "${_mmix_kernel_headers}/asm/unistd.h" OR
   NOT EXISTS "${_mmix_kernel_headers}/asm/unistd_64.h" OR
   NOT EXISTS "${_mmix_kernel_headers}/linux/errno.h")
  message(FATAL_ERROR
    "MMIX Linux libc requires kernel headers inside CMAKE_SYSROOT")
endif()
foreach(_mmix_header asm/unistd.h asm/unistd_64.h linux/errno.h)
  get_filename_component(_mmix_header_path "${_mmix_kernel_headers}/${_mmix_header}" REALPATH)
  string(FIND "${_mmix_header_path}" "${_mmix_sysroot}/" _mmix_header_prefix)
  if(NOT _mmix_header_prefix EQUAL 0 OR IS_DIRECTORY "${_mmix_header_path}")
    message(FATAL_ERROR "MMIX Linux libc requires kernel headers inside CMAKE_SYSROOT")
  endif()
endforeach()
unset(_mmix_sysroot)
unset(_mmix_kernel_headers)
unset(_mmix_header_prefix)
unset(_mmix_header)
unset(_mmix_header_path)

set(CMAKE_SYSTEM_NAME Linux CACHE STRING "")
set(CMAKE_SYSTEM_PROCESSOR mmix CACHE STRING "")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY CACHE STRING "")
# libc supplies its own C++ support and does not consume installed libc++.
set(CMAKE_CXX_FLAGS "-nostdinc++" CACHE STRING "")
set(CMAKE_C_COMPILER_TARGET mmix-unknown-linux CACHE STRING "")
set(CMAKE_CXX_COMPILER_TARGET mmix-unknown-linux CACHE STRING "")
set(CMAKE_ASM_COMPILER_TARGET mmix-unknown-linux CACHE STRING "")
set(LLVM_DEFAULT_TARGET_TRIPLE mmix-unknown-linux CACHE STRING "")
# libc's CMake parser expects an explicit environment to locate the OS.
set(LIBC_TARGET_TRIPLE mmix-unknown-linux-unknown CACHE STRING "")
set(LLVM_ENABLE_RUNTIMES libc CACHE STRING "")
set(LLVM_LIBC_FULL_BUILD ON CACHE BOOL "")
# FIXME: Replace this transitional single-thread state with per-thread errno
# and the pthread/TLS configuration when Linux runtime integration is ready.
set(LIBC_CONF_ERRNO_MODE LIBC_ERRNO_MODE_SHARED CACHE STRING "")
set(LIBC_CONF_THREAD_MODE LIBC_THREAD_MODE_SINGLE CACHE STRING "")
set(LLVM_INCLUDE_TESTS OFF CACHE BOOL "")
set(LLVM_INCLUDE_BENCHMARKS OFF CACHE BOOL "")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING "")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY CACHE STRING "")
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY CACHE STRING "")
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY CACHE STRING "")
