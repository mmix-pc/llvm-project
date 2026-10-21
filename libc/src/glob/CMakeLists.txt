add_entrypoint_object(
  glob
  SRCS glob.cpp
  HDRS glob.h
  DEPENDS
    libc.hdr.glob_macros
    libc.hdr.types.glob_t
    libc.hdr.errno_macros
    libc.hdr.fnmatch_macros
    libc.hdr.sys_stat_macros
    libc.hdr.func.free
    libc.hdr.func.realloc
    libc.src.__support.CPP.limits
    libc.src.__support.File.dir
    libc.src.__support.File.platform_dir
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.fnmatch.fnmatch
    libc.src.stdlib.qsort_util
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.string_utils
    libc.src.string.strcoll
    libc.src.sys.stat.stat
    libc.src.sys.stat.lstat
)

add_entrypoint_object(
  globfree
  SRCS globfree.cpp
  HDRS globfree.h
  DEPENDS
    libc.hdr.types.glob_t
    libc.hdr.func.free
    libc.src.__support.common
    libc.src.__support.libc_errno
)
