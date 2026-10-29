add_entrypoint_object(
  fmtmsg
  SRCS fmtmsg.cpp
  HDRS fmtmsg.h
  DEPENDS
    libc.hdr.fcntl_macros
    libc.src.__support.CPP.string_view
    libc.src.__support.File.file
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.write
    libc.src.__support.common
    libc.src.__support.macros.config
    libc.src.stdio.stderr
    libc.src.stdlib.getenv
)
