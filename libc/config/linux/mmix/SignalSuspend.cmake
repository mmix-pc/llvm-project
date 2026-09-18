option(LIBC_MMIX_BUILD_SIGNAL_SUSPEND "Build Linux signal suspension and membership providers" OFF)
if(LIBC_MMIX_BUILD_SIGNAL_SUSPEND)
  if(NOT LIBC_MMIX_BUILD_SIGNAL_MASKS)
    message(FATAL_ERROR "MMIX sigsuspend requires signal-mask support")
  endif()
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.signal.sigsuspend
    libc.src.signal.sigismember)
endif()
