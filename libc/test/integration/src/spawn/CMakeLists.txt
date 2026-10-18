add_custom_target(spawn-integration-tests)
add_dependencies(libc-integration-tests spawn-integration-tests)

if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
   LIBC_MMIX_BUILD_COORDINATED_FORK)
  add_integration_test(
    mmix_coordinated_spawn_test
    SUITE spawn-integration-tests
    SRCS mmix_coordinated_spawn_test.cpp
    DEPENDS
      libc.hdr.errno_macros
      libc.include.pthread
      libc.include.signal
      libc.include.spawn
      libc.include.sys_syscall
      libc.include.sys_wait
      libc.src.__support.CPP.atomic
      libc.src.__support.OSUtil.osutil
      libc.src.__support.threads.linux.mmix.fork_runtime
      libc.src.pthread.pthread_atfork
      libc.src.pthread.pthread_create
      libc.src.pthread.pthread_join
      libc.src.signal.sigaction
      libc.src.signal.sigaddset
      libc.src.signal.sigemptyset
      libc.src.signal.sigismember
      libc.src.signal.sigprocmask
      libc.src.spawn.posix_spawn
      libc.src.spawn.posix_spawnp
      libc.src.sys.wait.waitpid
      libc.src.unistd.environ
  )
endif()

add_integration_test(
  posix_spawnp_test
  SUITE spawn-integration-tests
  SRCS posix_spawnp_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.include.fcntl
    libc.include.spawn
    libc.include.sys_wait
    libc.src.__support.CPP.stringstream
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.spawn.posix_spawnp
    libc.src.spawn.posix_spawn_file_actions_addopen
    libc.src.spawn.posix_spawn_file_actions_destroy
    libc.src.spawn.posix_spawn_file_actions_init
    libc.src.stdlib.getenv
    libc.src.stdlib.mkdtemp
    libc.src.stdlib.setenv
    libc.src.stdlib.unsetenv
    libc.src.sys.stat.chmod
    libc.src.sys.stat.mkdir
    libc.src.sys.wait.waitpid
    libc.src.unistd.close
    libc.src.unistd.read
    libc.src.unistd.rmdir
    libc.src.unistd.symlink
    libc.src.unistd.unlink
    libc.src.unistd.write
)

add_executable(
  libc_posix_spawn_test_binary
  EXCLUDE_FROM_ALL
  posix_spawn_test_binary.cpp
  test_binary_properties.h
)
set_target_properties(
  libc_posix_spawn_test_binary
  PROPERTIES
    OUTPUT_NAME libc_posix_spawn_test_binary
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_header_library(
  test_binary_properties
  HDRS
    test_binary_properties.h
)

add_integration_test(
  posix_spawn_test
  SUITE
    spawn-integration-tests
  SRCS
    posix_spawn_test.cpp
  DEPENDS
    libc_posix_spawn_test_binary
    libc.test.integration.src.spawn.test_binary_properties
    libc.hdr.stdint_proxy
    libc.include.fcntl
    libc.include.signal
    libc.include.spawn
    libc.include.sys_wait
    libc.src.signal.raise
    libc.src.spawn.posix_spawn
    libc.src.spawn.posix_spawn_file_actions_addopen
    libc.src.spawn.posix_spawn_file_actions_destroy
    libc.src.spawn.posix_spawn_file_actions_init
    libc.src.sys.wait.waitpid
)

add_subdirectory(testdata)
