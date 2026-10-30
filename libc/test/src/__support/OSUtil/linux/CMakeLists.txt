if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_ARCHITECTURE})
  add_subdirectory(${LIBC_TARGET_ARCHITECTURE})
endif()

add_libc_test(
  notification_channels_test
  SUITE libc-osutil-tests
  SRCS notification_channels_test.cpp
  DEPENDS libc.src.__support.OSUtil.linux.notification_channels
    libc.src.errno.errno
)

add_libc_test(
  notification_transport_test
  SUITE libc-osutil-tests
  SRCS notification_transport_test.cpp
  DEPENDS
    libc.src.__support.OSUtil.linux.notification_transport
)

add_libc_test(
  aio_engine_test
  SUITE libc-osutil-tests
  SRCS aio_engine_test.cpp
  DEPENDS libc.src.__support.OSUtil.linux.aio_engine libc.src.errno.errno
    libc.src.__support.threads.aio_suspend_observers
    libc.src.__support.threads.aio_storage libc.hdr.func.aligned_alloc
)

add_libc_test(
  aio_completion_queue_test
  SUITE libc-osutil-tests
  SRCS aio_completion_queue_test.cpp
  DEPENDS
    libc.src.__support.OSUtil.linux.aio_completion_queue
    libc.src.errno.errno
)

add_libc_test(
  aio_submission_resources_test
  SUITE libc-osutil-tests
  SRCS aio_submission_resources_test.cpp
  DEPENDS
    libc.src.__support.OSUtil.linux.aio_submission_resources
    libc.src.errno.errno
)

add_libc_test(
  io_uring_test
  SUITE libc-osutil-tests
  SRCS io_uring_test.cpp
  DEPENDS
    libc.src.__support.OSUtil.linux.io_uring
    libc.src.errno.errno
)

add_libc_test(
  stat_via_statx_test
  SUITE libc-osutil-tests
  SRCS stat_via_statx_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.stdint_proxy
    libc.hdr.types.struct_stat
    libc.src.__support.CPP.limits
    libc.src.__support.OSUtil.linux.stat.kernel_statx_types
    libc.src.__support.OSUtil.linux.stat.stat_via_statx
    libc.src.__support.OSUtil.osutil
)

add_libc_test(
  rt_sigprocmask_test
  SUITE libc-osutil-tests
  SRCS rt_sigprocmask_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.rt_sigprocmask
)

add_libc_test(
  sysinfo_test
  SUITE libc-osutil-tests
  SRCS sysinfo_test.cpp
  DEPENDS
    libc.src.__support.CPP.string_view
    libc.src.__support.OSUtil.linux.sysinfo
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.unlink
    libc.src.unistd.write
)

add_libc_test(
  vdso_test
  SUITE libc-osutil-tests
  SRCS vdso_test.cpp
  DEPENDS
    libc.src.__support.OSUtil.linux.vdso
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.struct_sigaction
    libc.hdr.types.struct_timeval
    libc.hdr.types.struct_timespec
    libc.hdr.types.clockid_t
    libc.hdr.types.time_t
    libc.hdr.time_macros
    libc.hdr.signal_macros
    libc.src.signal.sigaction
    libc.src.signal.raise
)
