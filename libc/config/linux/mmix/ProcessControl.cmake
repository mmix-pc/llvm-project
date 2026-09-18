option(LIBC_MMIX_BUILD_PROCESS_CONTROL
  "Build Linux process queries, timed signal waits and terminal flush" OFF)
if(LIBC_MMIX_BUILD_PROCESS_CONTROL)
  if(NOT LIBC_MMIX_BUILD_SIGNAL_MASKS)
    message(FATAL_ERROR "MMIX process control requires signal-mask support")
  endif()
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.unistd.getppid
    libc.src.unistd.getpagesize
    libc.src.termios.tcflush
    libc.src.signal.sigtimedwait
    libc.src.sched.sched_getaffinity
    libc.src.sys.prctl.prctl)
endif()
