add_custom_target(libc_grp_unittests)
if(NOT TARGET libc.src.grp.lookup)
  return()
endif()
add_libc_test(
  query_test
  SUITE libc_grp_unittests
  SRCS
    query_test.cpp
    ${LIBC_SOURCE_DIR}/src/grp/getgrnam_r.cpp
    ${LIBC_SOURCE_DIR}/src/grp/getgrgid_r.cpp
    ${LIBC_SOURCE_DIR}/src/grp/getgrnam.cpp
    ${LIBC_SOURCE_DIR}/src/grp/getgrgid.cpp
  DEPENDS
    libc.src.grp.lookup
    libc.src.__support.common
    libc.src.stdio.remove
)
if(LIBC_TARGET_OS STREQUAL "linux")
  add_libc_test(
    management_test
    SUITE libc_grp_unittests
    SRCS
      management_test.cpp
      ${LIBC_SOURCE_DIR}/src/grp/getgrnam.cpp
      ${LIBC_SOURCE_DIR}/src/grp/getgrnam_r.cpp
    DEPENDS
      libc.src.grp.lookup
      libc.src.__support.common
      libc.src.__support.error_or
      libc.hdr.func.free
      libc.hdr.func.realloc
      libc.src.stdio.remove
  )
endif()
