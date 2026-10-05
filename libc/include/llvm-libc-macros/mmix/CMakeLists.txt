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
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    add_header(linux_signal_base_macros HDR linux-signal-macros.h
      DEST_HDR linux-signal-base-macros.h)
    add_header(linux_signal_macros HDR cancellation-signal-macros.h
      DEST_HDR linux-signal-macros.h DEPENDS .linux_signal_base_macros)
  else()
    add_header(linux_signal_macros HDR linux-signal-macros.h)
  endif()
  if(LIBC_MMIX_BUILD_UNWIND_ROOTS)
    add_header(pthread_base_macros HDR ../pthread-macros.h
      DEST_HDR pthread-base-macros.h)
    add_header(pthread_cleanup HDR pthread-cleanup.h)
  endif()
endif()
