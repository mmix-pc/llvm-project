add_custom_target(libc-support-threads-tests)

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
