option(LIBC_MMIX_BUILD_RUNTIME_STATE "Build a non-installed Linux state archive" OFF)
if(NOT LIBC_MMIX_BUILD_RUNTIME_STATE)
  return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/RuntimeMode.cmake)
mmix_check_runtime_mode()
if(NOT LLVM_LIBC_FULL_BUILD OR
   LIBC_CONF_TIMEOUT_ENSURE_MONOTONICITY OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "MMIX Linux state requires cross Release, full libc and untimed storage")
endif()
# Keep preparation distinct from libc.a and out of installation rules.
set(mmix_state_objects
  libc.src.errno.errno
  libc.src.__support.threads.thread
  libc.src.__support.threads.linux.mmix.main_thread
  libc.src.__support.OSUtil.linux.linux_util
  libc.src.__support.OSUtil.linux.mmix.syscall_asm)
if(LIBC_MMIX_ENABLE_STATIC_TLS)
  list(APPEND mmix_state_objects libc.src.__support.threads.linux.mmix.tls)
endif()
add_library(mmix_libc_state STATIC)
foreach(object IN LISTS mmix_state_objects)
  target_sources(mmix_libc_state PRIVATE $<TARGET_OBJECTS:${object}>)
endforeach()
set_target_properties(mmix_libc_state PROPERTIES
  MMIX_STATE_OBJECT_TARGETS "${mmix_state_objects}"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mmix-state")
export(TARGETS mmix_libc_state
  FILE "${CMAKE_BINARY_DIR}/mmix-state/LibcStateTargets.cmake")
configure_file("${CMAKE_CURRENT_LIST_DIR}/LibcStateConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/mmix-state/LibcStateConfig.cmake" @ONLY)
