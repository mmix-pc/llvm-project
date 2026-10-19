add_entrypoint_object(
  _Fork
  SRCS mmix/_Fork.cpp
  HDRS ../_Fork.h
  DEPENDS
    libc.hdr.types.pid_t
    libc.hdr.signal_macros
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.threads.linux.mmix.main_thread
    libc.src.__support.threads.linux.mmix.fork_transport
    libc.src.errno.errno
)
