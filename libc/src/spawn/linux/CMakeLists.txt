set(spawn_source posix_spawn.cpp)
set(spawn_signal_deps libc.src.signal.linux.signal_utils)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(spawn_source mmix/posix_spawn.cpp)
  set(spawn_signal_deps
    libc.hdr.errno_macros
    libc.src.signal.linux.mmix.rt_sigaction
    libc.src.__support.OSUtil.linux.syscall_wrappers.rt_sigprocmask)
endif()
add_entrypoint_object(
  posix_spawn
  SRCS
    ${spawn_source}
  HDRS
    ../posix_spawn.h
  DEPENDS
    libc.hdr.types.mode_t
    libc.hdr.fcntl_macros
    libc.hdr.signal_macros
    libc.include.spawn
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.dup2
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.spawn.file_actions
    ${spawn_signal_deps}
)
