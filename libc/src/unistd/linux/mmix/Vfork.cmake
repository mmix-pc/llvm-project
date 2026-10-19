set(mmix_vfork_dependencies)
if(LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM")
  list(APPEND mmix_vfork_dependencies
    libc.src.__support.threads.linux.mmix.lifecycle
    libc.src.__support.threads.linux.mmix.thread_create
    libc.src.__support.threads.linux.mmix.credentials
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.hdr.errno_macros)
endif()
add_entrypoint_object(
  vfork
  SRCS mmix/vfork.cpp mmix/vfork_error.cpp
  HDRS ../vfork.h
  DEPENDS
    ${mmix_vfork_dependencies}
    libc.hdr.types.pid_t
    libc.hdr.signal_macros
    libc.include.sys_syscall
    libc.src.__support.macros.macro_utils
    libc.src.errno.errno
  COMPILE_OPTIONS -fno-lto
)
