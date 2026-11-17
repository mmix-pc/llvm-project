add_custom_target(libc_termios_unittests)

add_libc_test(
  flow_flush_test
  SUITE libc_termios_unittests
  SRCS
    flow_flush_test.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/tcflow.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/tcflush.cpp
  HDRS termios_test_syscall.h
  COMPILE_OPTIONS
    -DLIBC_FULL_BUILD
    -include ${CMAKE_CURRENT_SOURCE_DIR}/termios_test_syscall.h
  DEPENDS
    libc.hdr.limits_macros
    libc.hdr.termios_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)

add_libc_test(
  winsize_test
  SUITE libc_termios_unittests
  SRCS
    winsize_test.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/tcgetwinsize.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/tcsetwinsize.cpp
  HDRS termios_test_syscall.h
  COMPILE_OPTIONS
    -DLIBC_FULL_BUILD
    -include ${CMAKE_CURRENT_SOURCE_DIR}/termios_test_syscall.h
  DEPENDS
    libc.hdr.types.struct_winsize
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.__support.macros.null_check
    libc.src.errno.errno
)

add_libc_test(
  tcsendbreak_test
  SUITE libc_termios_unittests
  SRCS
    tcsendbreak_test.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/tcsendbreak.cpp
  HDRS termios_test_syscall.h
  COMPILE_OPTIONS
    -DLIBC_FULL_BUILD
    -include ${CMAKE_CURRENT_SOURCE_DIR}/termios_test_syscall.h
  DEPENDS
    libc.hdr.limits_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)

add_libc_test(
  speed_test
  SUITE libc_termios_unittests
  SRCS speed_test.cpp
  DEPENDS
    libc.hdr.termios_macros
    libc.hdr.types.struct_termios
    libc.src.errno.errno
    libc.src.termios.cfgetispeed
    libc.src.termios.cfgetospeed
    libc.src.termios.cfsetispeed
    libc.src.termios.cfsetospeed
)

# Exercise the real conversion against a controlled kernel boundary, without
# requiring a controlling terminal or changing the test runner's attributes.
add_libc_test(
  termios_conversion_test
  SUITE libc_termios_unittests
  SRCS
    termios_conversion_test.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/tcgetattr.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/tcsetattr.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfgetispeed.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfgetospeed.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfsetispeed.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfsetospeed.cpp
  HDRS termios_test_syscall.h
  COMPILE_OPTIONS
    -DLIBC_FULL_BUILD
    -include ${CMAKE_CURRENT_SOURCE_DIR}/termios_test_syscall.h
  DEPENDS
    libc.hdr.termios_macros
    libc.hdr.types.struct_termios
    libc.src.termios.linux.kernel_termios
    libc.src.termios.linux.speed_utils
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.__support.macros.null_check
    libc.src.errno.errno
)

add_libc_test(
  termios_test
  SUITE
    libc_termios_unittests
  SRCS
    termios_test.cpp
  DEPENDS
    libc.include.termios
    libc.hdr.termios_macros
    libc.hdr.types.struct_termios
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.termios.cfgetispeed
    libc.src.termios.cfgetospeed
    libc.src.termios.cfsetispeed
    libc.src.termios.cfsetospeed
    libc.src.termios.tcgetattr
    libc.src.termios.tcgetsid
    libc.src.termios.tcsetattr
    libc.src.unistd.close
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  compatibility_test
  SUITE libc_termios_unittests
  SRCS
    compatibility_test.cpp
    ${LIBC_SOURCE_DIR}/src/termios/cfmakeraw.cpp
    ${LIBC_SOURCE_DIR}/src/termios/cfsetspeed.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfgetispeed.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfgetospeed.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfsetispeed.cpp
    ${LIBC_SOURCE_DIR}/src/termios/linux/cfsetospeed.cpp
  DEPENDS
    libc.hdr.termios_macros
    libc.hdr.types.struct_termios
    libc.src.termios.linux.speed_utils
    libc.src.__support.macros.null_check
    libc.src.errno.errno
)
