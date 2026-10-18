add_entrypoint_object(
  initgroups
  SRCS initgroups.cpp
  HDRS ../initgroups.h
  DEPENDS
    libc.src.grp.supplementary_groups
    libc.include.sys_syscall
    libc.src.__support.OSUtil.linux.syscall_wrappers.setgroups
    libc.src.__support.common
    libc.src.__support.libc_errno
)
