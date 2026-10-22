set(ppoll_source ppoll.cpp)
set(poll_source poll.cpp)
set(ppoll_private_deps)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(ppoll_source mmix/ppoll.cpp)
  set(poll_source mmix/poll.cpp)
  set(poll_private_deps libc.src.poll.ppoll)
  set(ppoll_private_deps libc.src.signal.linux.mmix.private_signals)
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    list(APPEND ppoll_private_deps libc.src.__support.threads.linux.mmix.cancellation_point)
    set(mmix_poll_options -DLIBC_MMIX_CANCELLATION_WINDOWS -fexceptions -funwind-tables)
  endif()
endif()

add_entrypoint_object(
  poll
  SRCS
    ${poll_source}
  HDRS
    ../poll.h
  DEPENDS
    libc.hdr.types.nfds_t
    libc.hdr.types.struct_pollfd
    libc.hdr.types.struct_timespec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    ${poll_private_deps}
  COMPILE_OPTIONS ${mmix_poll_options}
)

add_entrypoint_object(
  ppoll
  SRCS
    ${ppoll_source}
  HDRS
    ../ppoll.h
  DEPENDS
    ${ppoll_private_deps}
    libc.hdr.limits_macros
    libc.hdr.signal_macros
    libc.hdr.types.nfds_t
    libc.hdr.types.sigset_t
    libc.hdr.types.struct_pollfd
    libc.hdr.types.struct_timespec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
  COMPILE_OPTIONS ${mmix_poll_options}
)
