add_entrypoint_object(
  flock
  SRCS flock.cpp
  HDRS ../flock.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
