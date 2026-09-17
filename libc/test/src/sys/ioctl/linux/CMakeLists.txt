add_custom_target(libc_sys_ioctl_unittests)

set(arg_test_modes full)
if(NOT LLVM_LIBC_FULL_BUILD)
  list(PREPEND arg_test_modes overlay)
endif()
foreach(mode IN LISTS arg_test_modes)
  set(arg_test_options -include ${CMAKE_CURRENT_SOURCE_DIR}/ioctl_test_syscall.h)
  if(mode STREQUAL "full")
    list(APPEND arg_test_options -DLIBC_FULL_BUILD)
  endif()
  add_libc_test(
    ioctl_args_${mode}_test
    SUITE libc_sys_ioctl_unittests
    SRCS
      ioctl_args_test.cpp
      ${LIBC_SOURCE_DIR}/src/sys/ioctl/linux/ioctl.cpp
    HDRS ioctl_test_syscall.h
    COMPILE_OPTIONS ${arg_test_options}
    DEPENDS
      libc.hdr.sys_ioctl_macros
      libc.src.errno.errno
      libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
  )
endforeach()

add_libc_test(
  ioctl_test
  SUITE
    libc_sys_ioctl_unittests
  SRCS
    ioctl_test.cpp
  DEPENDS
    libc.hdr.types.struct_winsize
    libc.hdr.sys_ioctl_macros
    libc.src.sys.ioctl.ioctl
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.read
    libc.src.unistd.write
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)
