add_entrypoint_object(
  reboot
  SRCS reboot.cpp
  HDRS ../reboot.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
