# Opt-in build-tree C++ composition; aggregate installation is separate.
include("${CMAKE_CURRENT_LIST_DIR}/MMIXLinux.cmake")
set(LIBCXX_MMIX_LINUX_LIBC_BUILD "" CACHE PATH "Matching signal libc build")
set(LIBCXX_MMIX_LINUX_COMPILER_RT_BUILD "" CACHE PATH "Matching Linux compiler-rt build")
# Configuration builds the C++ headers; no installed C++ profile exists yet.
set(CMAKE_CXX_FLAGS "-nostdinc++" CACHE STRING "")
set(CMAKE_PROJECT_Runtimes_INCLUDE
  "${CMAKE_CURRENT_LIST_DIR}/../Modules/MMIX/LinuxRuntime.cmake" CACHE FILEPATH "" FORCE)
set(LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS ON CACHE BOOL "")
set(LIBCXXABI_ENABLE_RTTI ON CACHE BOOL "")
set(LIBUNWIND_IS_NATIVE_ONLY ON CACHE BOOL "")
set(LIBCXX_ABI_VERSION 1 CACHE STRING "")
set(LIBCXX_ABI_NAMESPACE __1 CACHE STRING "")
set(LIBCXX_ABI_UNSTABLE OFF CACHE BOOL "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER CACHE STRING "")
foreach(kind LIBRARY INCLUDE PACKAGE)
  set(CMAKE_FIND_ROOT_PATH_MODE_${kind} ONLY CACHE STRING "")
endforeach()
