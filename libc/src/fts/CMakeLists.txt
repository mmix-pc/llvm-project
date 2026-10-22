set(fts_selected FALSE)
foreach(name open read children set close)
  if("libc.src.fts.fts_${name}" IN_LIST TARGET_LIBC_ENTRYPOINTS)
    set(fts_selected TRUE)
  endif()
endforeach()
if("libc.src.ftw.ftw" IN_LIST TARGET_LIBC_ENTRYPOINTS OR
   "libc.src.ftw.nftw" IN_LIST TARGET_LIBC_ENTRYPOINTS)
  set(fts_selected TRUE)
endif()
if(NOT fts_selected)
  return()
endif()

add_object_library(
  engine
  SRCS engine.cpp
  HDRS engine.h linux_io.h
  COMPILE_OPTIONS -Wno-conversion -Wno-unused-parameter
  DEPENDS
    libc.hdr.types.FTS
    libc.hdr.types.struct_dirent
    libc.include.dirent
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.func.malloc
    libc.hdr.func.free
    libc.hdr.func.realloc
    libc.hdr.limits_macros
    libc.hdr.stdint_proxy
    libc.hdr.sys_stat_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fchdir
    libc.src.__support.OSUtil.linux.stat.stat_via_statx
    libc.src.__support.OSUtil.osutil
    libc.src.__support.libc_assert
    libc.src.__support.libc_errno
    libc.src.string.memmove
    libc.src.string.memset
    libc.src.string.strlen
    libc.src.string.strrchr
    libc.src.stdlib.qsort_util
)

add_entrypoint_object(
  fts_open
  SRCS fts_open.cpp
  HDRS fts_open.h
  DEPENDS .engine libc.hdr.types.FTS
)

add_entrypoint_object(
  fts_read
  SRCS fts_read.cpp
  HDRS fts_read.h
  DEPENDS .engine libc.hdr.types.FTS
)

add_entrypoint_object(
  fts_children
  SRCS fts_children.cpp
  HDRS fts_children.h
  DEPENDS .engine libc.hdr.types.FTS
)

add_entrypoint_object(
  fts_set
  SRCS fts_set.cpp
  HDRS fts_set.h
  DEPENDS .engine libc.hdr.types.FTS
)

add_entrypoint_object(
  fts_close
  SRCS fts_close.cpp
  HDRS fts_close.h
  DEPENDS .engine libc.hdr.types.FTS
)
