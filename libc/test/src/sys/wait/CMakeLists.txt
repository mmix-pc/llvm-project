add_custom_target(libc_sys_wait_unittests)

if(LIBC_TARGET_OS_IS_LINUX)
  add_libc_test(
    waitid_syscall_test
    SUITE libc_sys_wait_unittests
    SRCS waitid_syscall_test.cpp
    DEPENDS
      libc.hdr.errno_macros
      libc.hdr.signal_macros
      libc.hdr.sys_wait_macros
      libc.hdr.types.id_t
      libc.hdr.types.idtype_t
      libc.hdr.types.siginfo_t
      libc.include.sys_syscall
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.osutil
      libc.src.__support.common
      libc.src.errno.errno
  )
endif()

add_libc_test(
  waitpid_test
  SUITE
    libc_sys_wait_unittests
  SRCS
    waitpid_test.cpp
  DEPENDS
    libc.include.sys_wait
    libc.src.errno.errno
    libc.src.sys.wait.waitpid
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  wait4_test
  SUITE
    libc_sys_wait_unittests
  SRCS
    wait4_test.cpp
  DEPENDS
    libc.include.sys_wait
    libc.src.errno.errno
    libc.src.sys.wait.wait4
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)
