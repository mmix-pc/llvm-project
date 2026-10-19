set(ppoll_source ppoll.cpp)
set(ppoll_private_deps)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(ppoll_source mmix/ppoll.cpp)
  set(ppoll_private_deps libc.src.signal.linux.mmix.private_signals)
endif()

add_entrypoint_object(
  poll
  SRCS
    poll.cpp
  HDRS
    ../poll.h
  DEPENDS
    libc.hdr.types.nfds_t
    libc.hdr.types.struct_pollfd
    libc.hdr.types.struct_timespec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
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
)
