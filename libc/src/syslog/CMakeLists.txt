if(NOT LIBC_TARGET_OS_IS_LINUX)
  return()
endif()
set(selected_logging ${TARGET_LIBC_ENTRYPOINTS})
list(FILTER selected_logging INCLUDE REGEX "^libc\\.src\\.syslog\\.(syslog|vsyslog|openlog|closelog|setlogmask)$")
if(NOT selected_logging)
  return()
endif()
set(program_name_dep)
if(LLVM_LIBC_FULL_BUILD)
  set(program_name_dep libc.src.errno.program_invocation_short_name)
endif()
add_object_library(
  log
  SRCS linux/log.cpp
  HDRS log.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.sys_socket_macros
    libc.hdr.syslog_macros
    libc.hdr.types.struct_sockaddr_un
    libc.src.__support.CPP.mutex
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.threads.mutex
    libc.src.__support.libc_errno
    libc.src.fcntl.open
    libc.src.stdio.asprintf
    libc.src.stdio.vasprintf
    libc.hdr.func.free
    libc.src.sys.socket.socket
    libc.src.sys.socket.connect
    libc.src.sys.socket.send
    libc.src.time.localtime_r
    libc.src.time.time
    libc.src.unistd.close
    libc.src.unistd.getpid
    libc.src.unistd.write
    ${program_name_dep}
  COMPILE_OPTIONS -fexceptions -funwind-tables
)
foreach(entry syslog vsyslog openlog closelog setlogmask)
  add_entrypoint_object(
    ${entry}
    SRCS ${entry}.cpp
    HDRS ${entry}.h
    DEPENDS .log libc.src.__support.common
    COMPILE_OPTIONS -fexceptions -funwind-tables
  )
endforeach()
