add_entrypoint_object(
  times
  SRCS times.cpp
  HDRS ../times.h
  DEPENDS
    libc.hdr.types.clock_t
    libc.hdr.types.struct_tms
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
