option(LIBC_MMIX_BUILD_RUNTIME_STATE "Build a non-installed Linux state archive" OFF)
if(NOT LIBC_MMIX_BUILD_RUNTIME_STATE)
  return()
endif()
if(NOT LLVM_LIBC_FULL_BUILD OR
   NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_SHARED" OR
   NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE" OR
   LIBC_CONF_TIMEOUT_ENSURE_MONOTONICITY OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "MMIX Linux state requires cross Release, shared errno and untimed SINGLE mode")
endif()
# Keep preparation distinct from libc.a and out of installation rules.
add_library(mmix_libc_state STATIC
  $<TARGET_OBJECTS:libc.src.errno.errno>
  $<TARGET_OBJECTS:libc.src.__support.threads.thread>
  $<TARGET_OBJECTS:libc.src.__support.threads.linux.mmix.main_thread>
  $<TARGET_OBJECTS:libc.src.__support.OSUtil.linux.linux_util>
  $<TARGET_OBJECTS:libc.src.__support.OSUtil.linux.mmix.syscall_asm>)
set_target_properties(mmix_libc_state PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mmix-state")
export(TARGETS mmix_libc_state
  FILE "${CMAKE_BINARY_DIR}/mmix-state/LibcStateTargets.cmake")
configure_file("${CMAKE_CURRENT_LIST_DIR}/LibcStateConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/mmix-state/LibcStateConfig.cmake" @ONLY)
