add_custom_target(libc_spawn_unittests)

add_libc_test(
  posix_spawn_file_actions_test
  SUITE
    libc_spawn_unittests
  SRCS
    posix_spawn_file_actions_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.limits_macros
    libc.hdr.stdint_proxy
    libc.include.spawn
    libc.src.spawn.file_actions
    libc.src.spawn.posix_spawn_file_actions_addclose
    libc.src.spawn.posix_spawn_file_actions_adddup2
    libc.src.spawn.posix_spawn_file_actions_addopen
    libc.src.spawn.posix_spawn_file_actions_destroy
    libc.src.spawn.posix_spawn_file_actions_init
    libc.src.unistd.sysconf
)

# Register independently of the file-action failure test's early return below.
set(spawnattr_deps
  libc.hdr.errno_macros
  libc.hdr.limits_macros
  libc.hdr.sched_macros
  libc.hdr.types.struct_sched_param
  libc.include.spawn
  libc.src.__support.common
  libc.src.__support.libc_errno
  libc.src.errno.errno
)
set(spawnattr_sources)
set(spawnattr_providers)
foreach(api IN ITEMS
    init destroy getflags setflags getpgroup setpgroup
    getschedparam setschedparam getschedpolicy setschedpolicy
    getsigdefault setsigdefault getsigmask setsigmask)
  list(APPEND spawnattr_sources
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawnattr_${api}.cpp)
  list(APPEND spawnattr_providers libc.src.spawn.posix_spawnattr_${api})
endforeach()

if(LLVM_INCLUDE_TESTS AND LLVM_LIBC_FULL_BUILD AND
   LIBC_TARGET_OS_IS_LINUX AND NOT CMAKE_CROSSCOMPILING AND NOT LIBC_CROSSBUILD)
  # Use generated spawn.h and the real implementations even when these
  # entrypoints are not exported by the native target. As in the failure test
  # below, link LibcTest directly without the unused death-test executors.
  list(APPEND spawnattr_deps libc.test.UnitTest.LibcTest)
  if(TARGET libc.src.time.clock)
    list(APPEND spawnattr_deps libc.src.time.clock)
  endif()
  get_object_files_for_test(spawnattr_objects spawnattr_skipped
    ${spawnattr_deps})
  if(spawnattr_skipped)
    message(STATUS "Skipping spawn attribute test: ${spawnattr_skipped}")
  else()
    get_fq_target_name(posix_spawnattr_test spawnattr_target)
    set(spawnattr_binary ${spawnattr_target}.__build__)
    add_executable(${spawnattr_binary} EXCLUDE_FROM_ALL
      posix_spawnattr_test.cpp ${spawnattr_sources})
    target_include_directories(${spawnattr_binary} SYSTEM PRIVATE
      ${LIBC_INCLUDE_DIR})
    target_include_directories(${spawnattr_binary} PRIVATE
      ${LIBC_SOURCE_DIR} ${LIBC_BUILD_DIR})
    _get_common_test_compile_options(spawnattr_options "" "")
    target_compile_options(${spawnattr_binary} PRIVATE ${spawnattr_options})
    get_link_options(spawnattr_link_options
      ${spawnattr_options} ${LIBC_LINK_OPTIONS_DEFAULT}
      ${LIBC_TEST_LINK_OPTIONS_DEFAULT})
    target_link_options(${spawnattr_binary} PRIVATE ${spawnattr_link_options})
    target_link_libraries(${spawnattr_binary} PRIVATE ${spawnattr_objects})
    add_dependencies(${spawnattr_binary} ${spawnattr_deps})
    set_target_properties(${spawnattr_binary} PROPERTIES
      CXX_STANDARD ${CMAKE_CXX_STANDARD}
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    add_custom_target(${spawnattr_target}
      COMMAND $<TARGET_FILE:${spawnattr_binary}>
      DEPENDS ${spawnattr_binary}
      COMMENT "Running unit test ${spawnattr_target}")
    add_dependencies(libc_spawn_unittests ${spawnattr_target})
    add_dependencies(libc-unit-tests ${spawnattr_target})
    if(TARGET libc-unit-tests-build)
      add_dependencies(libc-unit-tests-build ${spawnattr_binary})
    endif()
  endif()
else()
  add_libc_test(
    posix_spawnattr_test
    SUITE
      libc_spawn_unittests
    SRCS
      posix_spawnattr_test.cpp
    DEPENDS
      ${spawnattr_deps}
      ${spawnattr_providers}
  )
endif()

if(LLVM_INCLUDE_TESTS AND LLVM_LIBC_FULL_BUILD AND
   LIBC_TARGET_OS_IS_LINUX AND NOT CMAKE_CROSSCOMPILING AND NOT LIBC_CROSSBUILD)
  # Both spawn directories are full-build-only. Keep that boundary: the test
  # must use LLVM's generated spawn.h, not the host's incompatible action type.
  # Link a focused native executable against the host allocator, since hermetic
  # test support implements free as a no-op. Reuse LibcTest without the unused
  # death-test executors pulled in by create_libc_unittest in a full build.
  # Compile the public action sources directly to substitute only sysconf;
  # malloc/free wrappers inject failures only during the calls under test.
  set(spawn_failure_deps
    libc.hdr.limits_macros
    libc.hdr.unistd_macros
    libc.src.spawn.file_actions
    libc.src.__support.CPP.new
    libc.src.__support.alloc_checker
    libc.src.errno.errno
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.string_utils
    libc.test.UnitTest.LibcTest
  )
  # The full-build LibcTest framework uses the internal clock for test timing.
  if(TARGET libc.src.time.clock)
    list(APPEND spawn_failure_deps libc.src.time.clock)
  endif()
  get_object_files_for_test(spawn_failure_objects spawn_failure_skipped
    ${spawn_failure_deps})
  if(spawn_failure_skipped)
    message(STATUS
      "Skipping spawn file action failure test: ${spawn_failure_skipped}")
    return()
  endif()

  get_fq_target_name(posix_spawn_file_actions_failure_test spawn_failure_target)
  set(spawn_failure_binary ${spawn_failure_target}.__build__)
  add_executable(${spawn_failure_binary} EXCLUDE_FROM_ALL
    posix_spawn_file_actions_failure_test.cpp
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawn_file_actions_init.cpp
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawn_file_actions_addopen.cpp
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawn_file_actions_adddup2.cpp
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawn_file_actions_addclose.cpp
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawn_file_actions_addchdir.cpp
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawn_file_actions_addfchdir.cpp
    ${LIBC_SOURCE_DIR}/src/spawn/posix_spawn_file_actions_destroy.cpp
  )
  target_include_directories(${spawn_failure_binary} SYSTEM PRIVATE
    ${LIBC_INCLUDE_DIR})
  target_include_directories(${spawn_failure_binary} PRIVATE
    ${LIBC_SOURCE_DIR} ${LIBC_BUILD_DIR})
  _get_common_test_compile_options(spawn_failure_options "" "")
  target_compile_options(${spawn_failure_binary} PRIVATE
    ${spawn_failure_options} -fno-builtin-malloc -fno-builtin-free)
  get_link_options(spawn_failure_link_options
    ${spawn_failure_options} ${LIBC_LINK_OPTIONS_DEFAULT}
    ${LIBC_TEST_LINK_OPTIONS_DEFAULT})
  target_link_options(${spawn_failure_binary} PRIVATE
    ${spawn_failure_link_options} -Wl,--wrap=malloc,--wrap=free)
  target_link_libraries(${spawn_failure_binary} PRIVATE ${spawn_failure_objects})
  add_dependencies(${spawn_failure_binary} ${spawn_failure_deps})
  set_target_properties(${spawn_failure_binary} PROPERTIES
    CXX_STANDARD ${CMAKE_CXX_STANDARD}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

  add_custom_target(${spawn_failure_target}
    COMMAND $<TARGET_FILE:${spawn_failure_binary}>
    DEPENDS ${spawn_failure_binary}
    COMMENT "Running unit test ${spawn_failure_target}")
  add_dependencies(libc_spawn_unittests ${spawn_failure_target})
  add_dependencies(libc-unit-tests ${spawn_failure_target})
  if(TARGET libc-unit-tests-build)
    add_dependencies(libc-unit-tests-build ${spawn_failure_binary})
  endif()
elseif(LLVM_INCLUDE_TESTS)
  message(STATUS
    "spawn file action failure test requires a native Linux full build")
endif()
