# Build-tree, main-thread static TLS composition, not pthread qualification.
set(LIBC_MMIX_ENABLE_STATIC_TLS ON CACHE BOOL "")
set(LIBC_CONF_THREAD_MODE LIBC_THREAD_MODE_PLATFORM CACHE STRING "")
set(LIBC_CONF_ERRNO_MODE LIBC_ERRNO_MODE_THREAD_LOCAL CACHE STRING "")
set(LIBC_COMPILE_OPTIONS_DEFAULT "-ftls-model=local-exec;-fno-lto" CACHE STRING "")
include(${CMAKE_CURRENT_LIST_DIR}/mmix-linux-c-runtime.cmake)
