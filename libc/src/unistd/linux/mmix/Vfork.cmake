add_entrypoint_object(
  vfork
  SRCS mmix/vfork.cpp mmix/vfork_error.cpp
  HDRS ../vfork.h
  DEPENDS
    libc.hdr.types.pid_t
    libc.hdr.signal_macros
    libc.include.sys_syscall
    libc.src.__support.macros.macro_utils
    libc.src.errno.errno
)
