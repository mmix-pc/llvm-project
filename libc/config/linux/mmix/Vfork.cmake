option(LIBC_MMIX_BUILD_VFORK "Build the Linux vfork syscall wrapper" OFF)
if(LIBC_MMIX_BUILD_VFORK)
  if(NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE")
    message(FATAL_ERROR "MMIX vfork preparation requires single-thread mode")
  endif()
  # Standalone SINGLE provider. PLATFORM composition selects its coordinated
  # admission separately; enabling this option does not qualify live siblings.
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.unistd.vfork)
endif()
