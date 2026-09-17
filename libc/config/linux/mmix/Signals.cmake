option(LIBC_MMIX_BUILD_SIGNAL_MASKS "Build Linux signal-set, mask and registration providers" OFF)
if(NOT LIBC_MMIX_BUILD_SIGNAL_MASKS)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.signal.sigemptyset
  libc.src.signal.sigfillset
  libc.src.signal.sigaddset
  libc.src.signal.sigdelset
  libc.src.signal.sigprocmask
  libc.src.signal.sigaction
  libc.src.signal.signal
  libc.src.signal.sigaltstack
  libc.src.signal.kill
  libc.src.signal.raise
  libc.src.unistd.alarm
  libc.src.string.strsignal
  libc.src.stdlib.abort
  libc.src.assert.__assert_fail
)
