option(LIBC_MMIX_BUILD_VFORK "Build the Linux vfork syscall wrapper" OFF)
if(LIBC_MMIX_BUILD_VFORK)
  if(NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE")
    message(FATAL_ERROR "MMIX vfork preparation requires single-thread mode")
  endif()
  # FIXME: The kernel currently rejects shared-VM clone. Wrapper admission does
  # not qualify child execution or register-stack/thread-state preservation.
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.unistd.vfork)
endif()
