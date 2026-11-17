option(LIBC_MMIX_BUILD_TERMIOS "Build Linux terminal attribute and speed providers" OFF)
if(LIBC_MMIX_BUILD_TERMIOS)
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.stdlib.posix_openpt
    libc.src.stdlib.grantpt
    libc.src.stdlib.unlockpt
    libc.src.stdlib.ptsname
    libc.src.stdlib.ptsname_r
    libc.src.termios.tcgetattr
    libc.src.termios.tcsetattr
    libc.src.termios.tcgetsid
    libc.src.termios.tcflow
    libc.src.termios.tcdrain
    libc.src.termios.tcgetwinsize
    libc.src.termios.tcsetwinsize
    libc.src.termios.tcsendbreak
    libc.src.termios.cfmakeraw
    libc.src.termios.cfsetspeed
    libc.src.termios.cfgetispeed
    libc.src.termios.cfgetospeed
    libc.src.termios.cfsetispeed
    libc.src.termios.cfsetospeed)
endif()
