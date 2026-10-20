add_custom_target(libc_shadow_unittests)
if(NOT TARGET libc.src.shadow.lookup)
  return()
endif()

add_libc_test(
  parse_test
  SUITE libc_shadow_unittests
  SRCS parse_test.cpp
  DEPENDS
    libc.src.shadow.lookup
    libc.src.__support.CPP.limits
    libc.src.__support.integer_to_string
)

add_libc_test(
  query_test
  SUITE libc_shadow_unittests
  SRCS
    query_test.cpp
    ${LIBC_SOURCE_DIR}/src/shadow/getspnam_r.cpp
    ${LIBC_SOURCE_DIR}/src/shadow/getspnam.cpp
  DEPENDS
    libc.src.shadow.lookup
    libc.src.__support.common
    libc.src.stdio.remove
    libc.src.unistd.dup
    libc.src.unistd.close
)

add_libc_test(
  failure_test
  SUITE libc_shadow_unittests
  SRCS failure_test.cpp
  DEPENDS
    libc.src.shadow.lookup
    libc.src.__support.common
)

# HermeticTestUtils uses an unsynchronized bump allocator with no-op free.
# Concurrent file allocation requires the host allocator and pthread runtime.
if(NOT LLVM_LIBC_FULL_BUILD AND LIBC_TARGET_OS_IS_LINUX AND NOT CMAKE_CROSSCOMPILING)
  add_libc_test(
    concurrency_test
    OVERLAY_BUILD_ONLY
    SUITE libc_shadow_unittests
    SRCS
      concurrency_test.cpp
      ${LIBC_SOURCE_DIR}/src/shadow/getspnam_r.cpp
      ${LIBC_SOURCE_DIR}/src/shadow/getspnam.cpp
    DEPENDS
      libc.src.shadow.lookup
      libc.src.__support.CPP.atomic
      libc.src.__support.common
      libc.src.stdio.remove
    LINK_LIBRARIES pthread
  )
endif()
