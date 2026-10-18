if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${LIBC_TARGET_OS})
endif()
add_custom_target(stdio-integration-tests)
add_dependencies(libc-integration-tests stdio-integration-tests)

if(LIBC_TARGET_OS STREQUAL "linux")
  add_integration_test(
    popen_registration_failure_test
    SUITE stdio-integration-tests
    SRCS popen_registration_failure_test.cpp
    DEPENDS
      libc.hdr.errno_macros
      libc.include.fcntl
      libc.src.errno.errno
      libc.src.fcntl.fcntl
      libc.src.stdio.linux.pipe_file
      libc.src.stdio.popen
      libc.src.unistd.close
  )
  if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
     LIBC_MMIX_BUILD_CANCELLATION_WINDOWS AND
     NOT LIBC_MMIX_BUILD_COORDINATED_FORK)
    add_integration_test(
      popen_admission_test
      SUITE stdio-integration-tests
      SRCS popen_admission_test.cpp
      DEPENDS
        libc.hdr.errno_macros
        libc.include.fcntl
        libc.src.errno.errno
        libc.src.fcntl.fcntl
        libc.src.stdio.popen
        libc.src.unistd.close
    )
  else()
    set(popen_test_options)
    if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
       LIBC_MMIX_BUILD_COORDINATED_FORK)
      set(popen_test_options -DLIBC_TEST_EXEC_ONLY_FORK_CHILD)
    endif()
    add_integration_test(
      popen_test
      SUITE stdio-integration-tests
      SRCS popen_test.cpp
      DEPENDS
        libc.src.stdio.linux.pipe_file
        libc.hdr.errno_macros
        libc.include.fcntl
        libc.include.signal
        libc.include.sys_resource
        libc.include.sys_syscall
        libc.include.sys_wait
        libc.src.__support.CPP.stringstream
        libc.src.__support.OSUtil.osutil
        libc.src.errno.errno
        libc.src.fcntl.fcntl
        libc.src.signal.sigaction
        libc.src.spawn.posix_spawn
        libc.src.stdio.fgetc
        libc.src.stdio.fileno
        libc.src.stdio.fputs
        libc.src.stdio.popen
        libc.src.stdio.pclose
        libc.src.sys.resource.getrlimit
        libc.src.sys.resource.setrlimit
        libc.src.sys.wait.waitpid
        libc.src.unistd.alarm
        libc.src.unistd.close
        libc.src.unistd.environ
        libc.src.unistd.execve
        libc.src.unistd.fork
      COMPILE_OPTIONS ${popen_test_options}
    )
  endif()
endif()

# These tests are not for correctness testing, but are instead a convenient way
# to generate hermetic binaries for comparitive binary size testing.
# FIXME: This test fails on AMDGPU so we disable it temporarily.
if(NOT LIBC_TARGET_ARCHITECTURE_IS_AMDGPU)
  add_integration_test(
    sprintf_size_test
    SUITE
      stdio-integration-tests
    SRCS
      sprintf_size_test.cpp
    DEPENDS
      libc.src.stdio.sprintf
    ARGS
      "%s %c %d"
      "First arg"
      "a"
      "0"
  )
endif()

add_integration_test(
  sprintf_size_test_no_sprintf
  SUITE
    stdio-integration-tests
  SRCS
    sprintf_size_test.cpp
  ARGS
    "%s %c %d"
    "First arg"
    "a"
    "0"
  COMPILE_OPTIONS
    -DINTEGRATION_DISABLE_PRINTF
)
