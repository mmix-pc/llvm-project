add_entrypoint_object(
  inotify_init1
  SRCS inotify_init1.cpp
  HDRS ../inotify_init1.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
add_entrypoint_object(
  inotify_add_watch
  SRCS inotify_add_watch.cpp
  HDRS ../inotify_add_watch.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    libc.hdr.stdint_proxy
)
add_entrypoint_object(
  inotify_rm_watch
  SRCS inotify_rm_watch.cpp
  HDRS ../inotify_rm_watch.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)
