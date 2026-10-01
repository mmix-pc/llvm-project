add_header(
  signal_macros
  HDR
    signal-macros.h
)

add_header(
  unistd_macros
  HDR
    unistd-macros.h
)

if(LIBC_TARGET_OS STREQUAL "linux")
  add_header(linux_signal_macros HDR linux-signal-macros.h)
  if(LIBC_MMIX_BUILD_UNWIND_ROOTS)
    add_header(pthread_base_macros HDR ../pthread-macros.h
      DEST_HDR pthread-base-macros.h)
    add_header(pthread_cleanup HDR pthread-cleanup.h)
  endif()
endif()
