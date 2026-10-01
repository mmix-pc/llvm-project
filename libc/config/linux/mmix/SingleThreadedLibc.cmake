include(${CMAKE_CURRENT_LIST_DIR}/RuntimeMode.cmake)
option(LIBC_MMIX_BUILD_C_RUNTIME "Compose the available Linux C runtime inputs" OFF)
if(NOT LIBC_MMIX_BUILD_C_RUNTIME)
  return()
endif()
mmix_check_runtime_mode()
if(LIBC_MMIX_ENABLE_STATIC_TLS OR LIBC_MMIX_BUILD_THREAD_LIFECYCLE OR
   LIBC_MMIX_BUILD_THREADED_LIBC)
  message(FATAL_ERROR "MMIX single-thread libc cannot compose TLS or thread profiles")
endif()

# Both the explicit selector and the legacy C-runtime selector use the same
# component requirements, CRT ownership, manifest and installation layout.
# Optional single-thread components remain controlled by their existing options.
include(${CMAKE_CURRENT_LIST_DIR}/CRuntime.cmake)
add_custom_target(mmix_libc_single_threaded DEPENDS mmix_libc_c_runtime)
