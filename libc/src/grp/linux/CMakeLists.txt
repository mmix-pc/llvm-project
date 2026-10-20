set(initgroups_source initgroups.cpp)
set(setgroups_source setgroups.cpp)
set(initgroups_platform_deps)
if(LIBC_TARGET_ARCHITECTURE_IS_MMIX AND
   LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM")
  set(initgroups_source mmix/initgroups.cpp)
  set(setgroups_source mmix/setgroups.cpp)
  list(APPEND initgroups_platform_deps
    libc.src.__support.threads.linux.mmix.credentials)
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

add_entrypoint_object(
  setgroups
  SRCS ${setgroups_source}
  HDRS ../setgroups.h
  DEPENDS
    libc.hdr.types.gid_t
    libc.hdr.types.size_t
    libc.include.sys_syscall
    libc.src.__support.OSUtil.linux.syscall_wrappers.setgroups
    libc.src.__support.common
    libc.src.__support.libc_errno
    ${initgroups_platform_deps}
)
