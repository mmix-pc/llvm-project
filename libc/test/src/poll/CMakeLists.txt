add_custom_target(libc_poll_unittests)

# These provider-body tests require LLVM libc's signal-set representation.
if(LIBC_TARGET_OS_IS_LINUX AND LLVM_LIBC_FULL_BUILD)
  foreach(mode IN ITEMS raw cancellable)
    set(readiness_options)
    if(mode STREQUAL "cancellable")
      list(APPEND readiness_options -DLIBC_MMIX_CANCELLATION_WINDOWS)
    endif()
    add_libc_test(
      mmix_readiness_${mode}_test
      SUITE libc_poll_unittests
      SRCS mmix_readiness_test.cpp
      DEPENDS
        libc.hdr.types.fd_set
        libc.hdr.sys_select_macros
        libc.hdr.types.sigset_t
        libc.hdr.types.nfds_t
        libc.hdr.types.struct_pollfd
        libc.hdr.types.struct_timespec
        libc.hdr.types.struct_timeval
        libc.include.sys_syscall
        libc.src.__support.CPP.limits
        libc.src.__support.OSUtil.osutil
        libc.src.errno.errno
      COMPILE_OPTIONS ${readiness_options}
    )
  endforeach()
endif()
if(LIBC_TARGET_OS_IS_LINUX)
  add_libc_test(
    ppoll_test
    SUITE libc_poll_unittests
    SRCS
      ppoll_test.cpp
      ${LIBC_SOURCE_DIR}/src/poll/linux/ppoll.cpp
    DEPENDS
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
endif()

add_libc_test(
  poll_test
  SUITE
    libc_poll_unittests
  SRCS
    poll_test.cpp
  DEPENDS
    libc.hdr.limits_macros
    libc.src.errno.errno
    libc.src.poll.poll
    libc.test.UnitTest.ErrnoCheckingTest
)
