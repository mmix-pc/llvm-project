add_custom_target(libc_poll_unittests)

if(LIBC_TARGET_OS_IS_LINUX)
  add_libc_test(
    ppoll_test
    SUITE libc_poll_unittests
    SRCS
      ppoll_test.cpp
      ${LIBC_SOURCE_DIR}/src/poll/linux/ppoll.cpp
    DEPENDS
      libc.hdr.limits_macros
      libc.hdr.signal_macros
      libc.hdr.types.nfds_t
      libc.hdr.types.sigset_t
      libc.hdr.types.struct_pollfd
      libc.hdr.types.struct_timespec
      libc.include.sys_syscall
      libc.src.__support.OSUtil.osutil
      libc.src.errno.errno
  )
endif()

add_libc_test(
  poll_test
  SUITE
    libc_poll_unittests
  SRCS
    poll_test.cpp
  DEPENDS
    libc.hdr.limits_macros
    libc.src.errno.errno
    libc.src.poll.poll
    libc.test.UnitTest.ErrnoCheckingTest
)
