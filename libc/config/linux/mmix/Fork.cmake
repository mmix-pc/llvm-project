option(LIBC_MMIX_BUILD_FORK "Build the Linux single-thread same-stack fork provider" OFF)
if(NOT LIBC_MMIX_BUILD_FORK)
  return()
endif()
if(NOT LIBC_MMIX_BUILD_SIGNAL_MASKS OR NOT LIBC_MMIX_BUILD_PROCESS_BASE OR
   NOT LIBC_MMIX_BUILD_RUNTIME_STATE OR
   NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE")
  message(FATAL_ERROR "MMIX Linux fork requires signal, process and single-thread runtime state providers")
endif()
list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.unistd.fork)
