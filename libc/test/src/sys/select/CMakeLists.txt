add_custom_target(libc_sys_select_unittests)

if(LIBC_TARGET_OS_IS_LINUX)
  add_libc_test(
    select_timeout_test
    SUITE libc_sys_select_unittests
    SRCS select_timeout_test.cpp
    DEPENDS
      libc.hdr.errno_macros
      libc.hdr.types.fd_set
      libc.hdr.types.sigset_t
      libc.hdr.types.size_t
      libc.hdr.types.struct_timespec
      libc.hdr.types.struct_timeval
      libc.include.sys_syscall
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.osutil
      libc.src.errno.errno
  )
endif()

add_libc_test(
  select_ui_test
  NO_RUN_POSTBUILD
  SUITE
    libc_sys_select_unittests
  SRCS
    select_ui_test.cpp
  DEPENDS
    libc.include.unistd
    libc.src.errno.errno
    libc.src.sys.select.select
    libc.src.unistd.read
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  select_failure_test
  SUITE
    libc_sys_select_unittests
  SRCS
    select_failure_test.cpp
  DEPENDS
    libc.include.unistd
    libc.src.errno.errno
    libc.src.sys.select.select
    libc.src.unistd.read
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_subdirectory(testdata)
