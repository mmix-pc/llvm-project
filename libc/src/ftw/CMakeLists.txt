if(NOT "libc.src.ftw.ftw" IN_LIST TARGET_LIBC_ENTRYPOINTS AND
   NOT "libc.src.ftw.nftw" IN_LIST TARGET_LIBC_ENTRYPOINTS)
  return()
endif()

add_object_library(
  walk
  SRCS linux/walk.cpp
  HDRS walk.h nftw.h
  DEPENDS
    libc.include.ftw
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.src.__support.CPP.limits
    libc.src.__support.OSUtil.linux.syscall_wrappers.chdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fchdir
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.errno.errno
    libc.src.fts.engine
    libc.src.__support.OSUtil.osutil
  COMPILE_OPTIONS -fexceptions -funwind-tables
)

foreach(name ftw nftw)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .walk libc.include.ftw
    COMPILE_OPTIONS -fexceptions -funwind-tables
  )
endforeach()
