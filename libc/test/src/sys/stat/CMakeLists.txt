add_custom_target(libc_sys_stat_unittests)

add_subdirectory(testdata)

if(LIBC_TARGET_OS STREQUAL "linux")
  foreach(variant IN ITEMS default legacy modern unavailable)
    set(fchmodat_options "")
    if(variant STREQUAL "legacy" OR variant STREQUAL "unavailable")
      list(APPEND fchmodat_options -DLIBC_TEST_FCHMODAT_NO_MODERN)
    endif()
    if(variant STREQUAL "modern" OR variant STREQUAL "unavailable")
      list(APPEND fchmodat_options -DLIBC_TEST_FCHMODAT_NO_LEGACY)
    endif()
    add_libc_test(
      fchmodat_syscall_${variant}_test
      SUITE libc_sys_stat_unittests
      SRCS fchmodat_syscall_test.cpp
      COMPILE_OPTIONS ${fchmodat_options}
      DEPENDS
        libc.hdr.errno_macros
        libc.hdr.fcntl_macros
        libc.hdr.types.mode_t
        libc.src.__support.OSUtil.linux.syscall_wrappers.fchmodat
        libc.src.__support.common
        libc.src.errno.errno
    )
  endforeach()

  add_libc_test(
    mmix_futimens_test
    SUITE libc_sys_stat_unittests
    SRCS
      linux/mmix/futimens_test.cpp
      ${LIBC_SOURCE_DIR}/src/sys/stat/linux/mmix/futimens.cpp
    DEPENDS
      libc.src.__support.OSUtil.linux.syscall_wrappers.utimensat
      libc.src.__support.common
      libc.src.__support.libc_errno
      libc.src.fcntl.open
      libc.src.sys.stat.fstat
      libc.src.unistd.close
      libc.src.unistd.unlink
  )
endif()

add_libc_test(
  umask_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    umask_test.cpp
  DEPENDS
    libc.hdr.types.mode_t
    libc.src.errno.errno
    libc.src.sys.stat.umask
)

add_libc_test(
  chmod_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    chmod_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_stat
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.sys.stat.chmod
    libc.src.unistd.close
    libc.src.unistd.write
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  fchmodat_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    fchmodat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_stat
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.sys.stat.fchmodat
    libc.src.sys.stat.fstat
    libc.src.unistd.close
    libc.src.unistd.symlink
    libc.src.unistd.unlink
    libc.src.unistd.write
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  fchmod_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    fchmod_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_stat
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.sys.stat.fchmod
    libc.src.unistd.close
    libc.src.unistd.write
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  mkfifo_test
  SUITE libc_sys_stat_unittests
  SRCS mkfifo_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.src.fcntl.open
    libc.src.sys.stat.fstatat
    libc.src.sys.stat.mkfifo
    libc.src.sys.stat.mkfifoat
    libc.src.sys.stat.umask
    libc.src.unistd.close
    libc.src.unistd.getcwd
    libc.src.unistd.unlink
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  mknodat_test
  SUITE libc_sys_stat_unittests
  SRCS mknodat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.mknodat
    libc.src.fcntl.open
    libc.src.sys.stat.fstatat
    libc.src.sys.stat.mknod
    libc.src.sys.stat.mknodat
    libc.src.unistd.close
    libc.src.unistd.getcwd
    libc.src.unistd.unlink
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  mkdirat_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    mkdirat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.src.errno.errno
    libc.src.sys.stat.mkdirat
    libc.src.unistd.rmdir
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  stat_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    stat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_stat
    libc.src.errno.errno
    libc.src.sys.stat.stat
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.unlink
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  fstatat_test
  SUITE libc_sys_stat_unittests
  SRCS fstatat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.src.sys.stat.fstatat
    libc.src.sys.stat.fstat
    libc.src.sys.stat.utimensat
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.getcwd
    libc.src.unistd.symlink
    libc.src.unistd.unlink
    libc.src.unistd.write
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  lstat_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    lstat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_stat
    libc.src.errno.errno
    libc.src.sys.stat.lstat
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.unlink
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  fstat_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    fstat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_stat
    libc.src.errno.errno
    libc.src.sys.stat.fstat
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.unlink
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  utimensat_test
  SUITE
    libc_sys_stat_unittests
  SRCS
    utimensat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_timespec
    libc.hdr.sys_stat_macros
    libc.hdr.types.struct_stat
    libc.src.errno.errno
    libc.src.sys.stat.utimensat
    libc.src.fcntl.open
    libc.src.sys.stat.stat
    libc.src.unistd.close
    libc.src.stdio.remove
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)
