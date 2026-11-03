set(tty_cancel_dependency)
set(tty_cancel_options)
if(TARGET libc.src.__support.threads.linux.mmix.cancellation)
  set(tty_cancel_dependency libc.src.__support.threads.linux.mmix.cancellation)
  set(tty_cancel_options -DLIBC_MMIX_TTY_CANCELLATION -fexceptions -funwind-tables)
endif()
add_entrypoint_object(
  ttyname_r
  SRCS mmix/ttyname_r.cpp
  HDRS ../ttyname_r.h
  DEPENDS
    libc.hdr.errno_macros libc.hdr.fcntl_macros libc.hdr.limits_macros
    libc.hdr.sys_ioctl_macros libc.hdr.sys_stat_macros
    libc.hdr.types.struct_dirent
    libc.src.__support.CPP.stringstream
    libc.src.__support.OSUtil.linux.stat.kernel_statx_types
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.readlinkat
    libc.src.__support.OSUtil.linux.syscall_wrappers.statx
    libc.src.string.memory_utils.inline_memcpy
    ${tty_cancel_dependency}
  COMPILE_OPTIONS ${tty_cancel_options}
)
add_entrypoint_object(
  ttyname
  SRCS mmix/ttyname.cpp
  HDRS ../ttyname.h
  DEPENDS
    libc.hdr.limits_macros
    libc.src.errno.errno
    .ttyname_r
  COMPILE_OPTIONS ${tty_cancel_options}
)
