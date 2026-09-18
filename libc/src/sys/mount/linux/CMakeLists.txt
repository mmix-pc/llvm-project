add_entrypoint_object(
  mount
  SRCS mount.cpp
  HDRS ../mount.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  umount2
  SRCS umount2.cpp
  HDRS ../umount2.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  umount
  SRCS umount.cpp
  HDRS ../umount.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
