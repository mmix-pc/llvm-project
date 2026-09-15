add_custom_target(libc_fcntl_unittests)

add_subdirectory(testdata)

if(LIBC_TARGET_OS_IS_LINUX)
  set(arg_test_modes full)
  if(NOT LLVM_LIBC_FULL_BUILD)
    list(PREPEND arg_test_modes overlay)
  endif()
  foreach(mode IN LISTS arg_test_modes)
    set(arg_test_options
      -include ${CMAKE_CURRENT_SOURCE_DIR}/fcntl_test_syscall.h)
    if(mode STREQUAL "full")
      list(APPEND arg_test_options -DLIBC_FULL_BUILD)
    endif()
    add_libc_test(
      fcntl_args_${mode}_test
      SUITE
        libc_fcntl_unittests
      SRCS
        fcntl_args_test.cpp
        ${LIBC_SOURCE_DIR}/src/fcntl/linux/fcntl.cpp
      HDRS
        fcntl_test_syscall.h
      COMPILE_OPTIONS
        ${arg_test_options}
      DEPENDS
        libc.hdr.stdint_proxy
        libc.src.errno.errno
        libc.src.__support.OSUtil.linux.syscall_wrappers.fcntl
    )
  endforeach()
endif()

add_libc_test(
  creat_test
  SUITE
    libc_fcntl_unittests
  SRCS
    creat_test.cpp
  DEPENDS
    libc.hdr.sys_stat_macros
    libc.include.fcntl
    libc.src.errno.errno
    libc.src.fcntl.creat
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  fcntl_test
  SUITE
    libc_fcntl_unittests
  SRCS
    fcntl_test.cpp
  DEPENDS
    libc.include.fcntl
    libc.src.errno.errno
    libc.src.fcntl.fcntl
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.getpid
    libc.src.unistd.getpgid
    libc.hdr.types.struct_f_owner_ex
    libc.hdr.types.struct_flock
    libc.hdr.fcntl_macros
    libc.hdr.stdio_macros
    libc.hdr.sys_stat_macros
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  openat_test
  SUITE
    libc_fcntl_unittests
  SRCS
    openat_test.cpp
  DEPENDS
    libc.hdr.fcntl_macros
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.fcntl.openat
    libc.src.unistd.close
    libc.src.unistd.read
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)
