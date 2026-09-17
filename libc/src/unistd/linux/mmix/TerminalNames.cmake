add_entrypoint_object(
  ttyname_r
  SRCS mmix/ttyname_r.cpp
  HDRS ../ttyname_r.h
  DEPENDS
    libc.hdr.sys_stat_macros
    libc.src.__support.CPP.stringstream
    libc.src.errno.errno
    libc.src.unistd.isatty
    libc.src.unistd.readlink
    libc.src.sys.stat.fstat
    libc.src.sys.stat.stat
)
add_entrypoint_object(
  ttyname
  SRCS mmix/ttyname.cpp
  HDRS ../ttyname.h
  DEPENDS
    libc.hdr.limits_macros
    libc.src.errno.errno
    .ttyname_r
)
