option(LIBC_MMIX_BUILD_TERMIOS "Build Linux terminal attribute and speed providers" OFF)
if(LIBC_MMIX_BUILD_TERMIOS)
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.termios.tcgetattr
    libc.src.termios.tcsetattr
    libc.src.termios.cfgetispeed
    libc.src.termios.cfgetospeed
    libc.src.termios.cfsetispeed
    libc.src.termios.cfsetospeed)
endif()
