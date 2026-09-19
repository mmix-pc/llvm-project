option(LIBC_MMIX_BUILD_LOCAL_LOGGING
  "Build Linux Unix-domain syslog client providers" OFF)
if(NOT LIBC_MMIX_BUILD_LOCAL_LOGGING)
  return()
endif()
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.syslog.syslog
  libc.src.syslog.vsyslog
  libc.src.syslog.openlog
  libc.src.syslog.closelog
  libc.src.syslog.setlogmask
  libc.src.sys.socket.socket
  libc.src.sys.socket.connect
  libc.src.sys.socket.send)
