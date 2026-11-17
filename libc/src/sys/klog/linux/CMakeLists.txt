add_entrypoint_object(
  klogctl
  SRCS klogctl.cpp
  HDRS ../klogctl.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
