add_custom_target(libc_mntent_unittests)
if(NOT TARGET libc.src.mntent.mntent_utils)
  return()
endif()
add_libc_test(
  mntent_test
  SUITE libc_mntent_unittests
  SRCS
    mntent_test.cpp
    ${LIBC_SOURCE_DIR}/src/mntent/setmntent.cpp
    ${LIBC_SOURCE_DIR}/src/mntent/getmntent.cpp
    ${LIBC_SOURCE_DIR}/src/mntent/getmntent_r.cpp
    ${LIBC_SOURCE_DIR}/src/mntent/endmntent.cpp
    ${LIBC_SOURCE_DIR}/src/mntent/hasmntopt.cpp
  DEPENDS
    libc.src.mntent.mntent_utils
    libc.hdr.types.FILE
    libc.hdr.func.realloc
    libc.src.__support.common
    libc.src.stdio.remove
    libc.src.stdio.fdopen
    libc.src.unistd.pipe
    libc.src.unistd.write
    libc.src.unistd.dup
    libc.src.unistd.close
)
