set(initgroups_source initgroups.cpp)
set(initgroups_platform_deps)
if(LIBC_TARGET_ARCHITECTURE_IS_MMIX)
  set(initgroups_source mmix/initgroups.cpp)
  list(APPEND initgroups_platform_deps
    libc.src.__support.threads.linux.mmix.process_operation_h)
endif()

add_entrypoint_object(
  initgroups
  SRCS ${initgroups_source}
  HDRS ../initgroups.h
  DEPENDS
    libc.src.grp.supplementary_groups
    libc.include.sys_syscall
    libc.src.__support.OSUtil.linux.syscall_wrappers.setgroups
    libc.src.__support.common
    libc.src.__support.libc_errno
    ${initgroups_platform_deps}
)
