set(select_source select.cpp)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(select_source mmix/select.cpp)
  set(mmix_select_deps libc.src.sys.select.pselect)
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    set(mmix_select_options -DLIBC_MMIX_CANCELLATION_WINDOWS -fexceptions -funwind-tables)
    set(mmix_select_cancel_dep libc.src.__support.threads.linux.mmix.cancellation_point)
  endif()
  add_entrypoint_object(
    pselect
    SRCS mmix/pselect.cpp
    HDRS ../pselect.h
    DEPENDS
      libc.hdr.sys_select_macros
      libc.hdr.types.fd_set libc.hdr.types.sigset_t
      libc.hdr.types.struct_timespec libc.include.sys_syscall
      libc.src.__support.OSUtil.osutil libc.src.errno.errno
      libc.src.signal.linux.mmix.private_signals
      ${mmix_select_cancel_dep}
    COMPILE_OPTIONS ${mmix_select_options}
  )
endif()

add_entrypoint_object(
  select
  SRCS
    ${select_source}
  HDRS
    ../select.h
  DEPENDS
    libc.hdr.types.fd_set
    libc.hdr.types.sigset_t
    libc.hdr.types.size_t
    libc.hdr.types.struct_timespec
    libc.hdr.types.struct_timeval
    libc.include.sys_syscall
    libc.src.__support.CPP.limits
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    ${mmix_select_deps}
  COMPILE_OPTIONS ${mmix_select_options}
)
