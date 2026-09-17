add_custom_target(libc_termios_unittests)

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
