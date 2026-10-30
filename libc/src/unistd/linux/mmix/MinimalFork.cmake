set(mmix_minimal_fork_dependencies)
set(mmix_minimal_fork_options)
if(LIBC_MMIX_BUILD_COORDINATED_FORK AND LIBC_MMIX_BUILD_UNWIND_ROOTS)
  set(mmix_minimal_fork_dependencies
    libc.src.__support.threads.linux.mmix.fork_runtime)
  set(mmix_minimal_fork_options
    -DLIBC_MMIX_RECOVERABLE_MINIMAL_FORK -funwind-tables)
endif()
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
    ${mmix_minimal_fork_dependencies}
  COMPILE_OPTIONS ${mmix_minimal_fork_options}
)
