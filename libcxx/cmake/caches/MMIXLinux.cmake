# Retained single-thread Linux profile with the transitional external adapter.
include("${CMAKE_CURRENT_LIST_DIR}/MMIXLinuxBase.cmake")
set(LIBCXX_MMIX_LINUX_STATE_DIR "" CACHE PATH "Matching libc build-tree state package")
set(CMAKE_PROJECT_Runtimes_INCLUDE
  "${CMAKE_CURRENT_LIST_DIR}/../Modules/MMIX/InstalledLibc.cmake" CACHE FILEPATH "")
# FIXME: Retire the adapter after the pthread C++ composition is qualified.
set(LIBCXX_HAS_EXTERNAL_THREAD_API ON CACHE BOOL "")
set(LIBCXX_HAS_PTHREAD_API OFF CACHE BOOL "")
set(LIBCXXABI_ENABLE_THREADS OFF CACHE BOOL "")
set(LIBCXXABI_HAS_PTHREAD_API OFF CACHE BOOL "")
set(LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL OFF CACHE BOOL "")
set(LIBUNWIND_ENABLE_THREADS OFF CACHE BOOL "")
