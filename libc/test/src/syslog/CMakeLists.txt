add_custom_target(libc-syslog-tests)
add_libc_test(
  local_log_test
  SUITE libc-syslog-tests
  SRCS local_log_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.sys_socket_macros
    libc.hdr.syslog_macros
    libc.hdr.types.struct_sockaddr_un
    libc.src.__support.CPP.mutex
    libc.src.__support.threads.mutex
    libc.src.errno.errno
    libc.src.errno.program_invocation_short_name
    libc.src.stdio.asprintf
    libc.src.stdio.vasprintf
    libc.hdr.func.free
)
