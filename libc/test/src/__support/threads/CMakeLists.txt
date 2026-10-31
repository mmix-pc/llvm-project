add_custom_target(libc-support-threads-tests)

if(LIBC_TARGET_OS_IS_LINUX AND NOT LLVM_LIBC_FULL_BUILD)
  add_libc_test(
    aio_transfer_test
    SUITE libc-support-threads-tests
    SRCS aio_transfer_test.cpp
    DEPENDS libc.include.aio libc.hdr.errno_macros libc.hdr.fcntl_macros
      libc.hdr.signal_macros libc.hdr.sys_socket_macros libc.hdr.sys_stat_macros
      libc.hdr.types.socklen_t libc.hdr.stdint_proxy libc.src.__support.CPP.new
      libc.src.__support.OSUtil.linux.stat.kernel_statx_types
      libc.src.__support.OSUtil.linux.syscall_wrappers.fstatfs
      libc.src.__support.OSUtil.linux.syscall_wrappers.statx
      libc.src.__support.macros.attributes libc.src.__support.macros.config
  )
  add_libc_test(
    aio_input_test
    SUITE libc-support-threads-tests
    SRCS aio_input_test.cpp
    DEPENDS libc.include.aio libc.hdr.errno_macros libc.hdr.signal_macros
      libc.hdr.stdint_proxy libc.src.__support.CPP.new
      libc.src.__support.macros.attributes libc.src.__support.macros.config
  )
endif()

add_libc_test(
  aio_file_dependencies_test
  SUITE libc-support-threads-tests
  SRCS aio_file_dependencies_test.cpp
  DEPENDS libc.src.__support.threads.aio_file_dependencies
)

add_libc_test(
  aio_completion_budget_test
  SUITE libc-support-threads-tests
  SRCS aio_completion_budget_test.cpp
  DEPENDS libc.src.__support.threads.aio_completion_budget
)

add_libc_test(
  aio_request_test
  SUITE libc-support-threads-tests
  SRCS aio_request_test.cpp
  DEPENDS libc.src.__support.threads.aio_request
)

add_libc_test(
  aio_wait_set_test
  SUITE libc-support-threads-tests
  SRCS aio_wait_set_test.cpp
  DEPENDS libc.src.__support.threads.aio_wait_set
)

add_libc_test(
  aio_suspend_observers_test
  SUITE libc-support-threads-tests
  SRCS aio_suspend_observers_test.cpp
  DEPENDS libc.src.__support.threads.aio_suspend_observers
)

add_libc_test(
  aio_storage_test
  SUITE libc-support-threads-tests
  SRCS aio_storage_test.cpp
  DEPENDS libc.src.__support.threads.aio_storage
    libc.hdr.func.aligned_alloc libc.hdr.func.free
)

add_libc_test(
  notification_registry_test
  SUITE
    libc-support-threads-tests
  SRCS
    notification_registry_test.cpp
  DEPENDS
    libc.src.__support.threads.notification_registry
)

if(TARGET libc.src.__support.threads.unix_mutex)
  add_libc_test(
    fork_owner_test
    SUITE libc-support-threads-tests
    SRCS fork_owner_test.cpp
    DEPENDS
      libc.src.__support.threads.unix_mutex
      libc.src.__support.threads.identifier
  )
endif()

if(TARGET libc.src.__support.threads.${LIBC_TARGET_OS}.futex_utils)
  if(LIBC_TARGET_OS_IS_LINUX AND NOT LLVM_LIBC_FULL_BUILD)
    add_libc_test(
      aio_service_registry_test
      SUITE libc-support-threads-tests
      SRCS aio_service_registry_test.cpp
      DEPENDS libc.src.__support.OSUtil.linux.aio_engine
        libc.src.__support.OSUtil.linux.aio_wait_event
        libc.src.__support.threads.aio_suspend_observers
        libc.src.__support.threads.aio_storage
        libc.src.__support.threads.raw_mutex
        libc.src.__support.OSUtil.osutil libc.hdr.signal_macros
        libc.hdr.func.aligned_alloc libc.hdr.func.free
    )
    add_libc_test(
      aio_suspend_registration_test
      SUITE libc-support-threads-tests
      SRCS aio_suspend_registration_test.cpp
      DEPENDS libc.src.__support.OSUtil.linux.aio_wait_event
        libc.src.__support.threads.aio_suspend_observers
        libc.src.__support.threads.raw_mutex
        libc.src.__support.OSUtil.osutil libc.hdr.signal_macros
        libc.src.__support.CPP.new
    )
    add_libc_test(
      aio_service_relative_test
      SUITE libc-support-threads-tests
      SRCS aio_service_relative_test.cpp
      DEPENDS libc.include.aio libc.hdr.signal_macros
        libc.src.__support.OSUtil.linux.aio_wait_event
        libc.src.__support.threads.aio_request
        libc.src.__support.CPP.limits libc.src.__support.CPP.new
        libc.src.__support.libc_errno
    )
    add_libc_test(
      aio_gate_test
      SUITE libc-support-threads-tests
      SRCS aio_gate_test.cpp
      DEPENDS libc.src.__support.threads.raw_mutex
        libc.src.__support.OSUtil.osutil libc.hdr.signal_macros
    )
  endif()
  add_libc_test(
    futex_utils_test
    SUITE
      libc-support-threads-tests
    SRCS
      futex_utils_test.cpp
    DEPENDS
      libc.src.__support.threads.futex_utils
  )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${LIBC_TARGET_OS})
endif()
