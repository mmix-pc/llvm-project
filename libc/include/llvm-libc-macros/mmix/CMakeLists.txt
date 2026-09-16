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
endif()
