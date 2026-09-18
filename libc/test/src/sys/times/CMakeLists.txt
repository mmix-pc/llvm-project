add_custom_target(libc_sys_times_unittests)
add_libc_test(
  times_test
  SUITE libc_sys_times_unittests
  SRCS times_test.cpp ${LIBC_SOURCE_DIR}/src/sys/times/linux/times.cpp
  DEPENDS
    libc.hdr.types.clock_t
    libc.hdr.types.struct_tms
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
