if(NOT TARGET libc.src.utmp.utmp_engine)
  return()
endif()

add_custom_target(libc_utmpx_unittests)

add_libc_test(
  utmpx_test
  SUITE libc_utmpx_unittests
  SRCS
    utmpx_test.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/setutxent.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/getutxent.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/endutxent.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/getutxid.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/getutxline.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/pututxline.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/utmpxname.cpp
    ${LIBC_SOURCE_DIR}/src/utmpx/updwtmpx.cpp
    ${LIBC_SOURCE_DIR}/src/utmp/endutent.cpp
    ${LIBC_SOURCE_DIR}/src/utmp/getutent.cpp
    ${LIBC_SOURCE_DIR}/src/utmp/pututline.cpp
    ${LIBC_SOURCE_DIR}/src/utmp/setutent.cpp
    ${LIBC_SOURCE_DIR}/src/utmp/updwtmp.cpp
    ${LIBC_SOURCE_DIR}/src/utmp/utmpname.cpp
  HDRS ${LIBC_SOURCE_DIR}/test/src/utmp/utmp_test.h
  DEPENDS
    libc.src.utmp.utmp_engine
    libc.src.__support.common
    libc.hdr.types.struct_utmpx
    libc.hdr.types.struct_utmp
    libc.src.__support.threads.thread
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.stdio_macros
    libc.hdr.types.struct_flock
    libc.hdr.utmp_macros
    libc.src.__support.libc_errno
    libc.src.fcntl.fcntl
    libc.src.stdio.rename
    libc.src.stdlib.mkstemp
    libc.src.sys.stat.fchmod
    libc.src.unistd.close
    libc.src.unistd.ftruncate
    libc.src.unistd.lseek
    libc.src.unistd.read
    libc.src.unistd.unlink
    libc.src.unistd.write
)
