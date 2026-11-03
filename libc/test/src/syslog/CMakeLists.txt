add_custom_target(libc-syslog-tests)
set(log_test_options)
set(log_test_program_name)
if(NOT LLVM_LIBC_FULL_BUILD)
  set(log_test_options -fexceptions -funwind-tables)
else()
  set(log_test_program_name libc.src.errno.program_invocation_short_name)
endif()
foreach(test local_log_test local_log_policy_test)
add_libc_test(
  ${test}
  SUITE libc-syslog-tests
  SRCS ${test}.cpp
    ${LIBC_SOURCE_DIR}/src/stdio/asprintf.cpp
    ${LIBC_SOURCE_DIR}/src/stdio/vasprintf.cpp
  COMPILE_OPTIONS ${log_test_options}
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.sys_socket_macros
    libc.hdr.syslog_macros
    libc.hdr.types.struct_sockaddr_un
    libc.src.__support.CPP.mutex
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.threads.mutex
    libc.src.errno.errno
    ${log_test_program_name}
    libc.src.__support.printf_core.vasprintf_internal
    libc.src.__support.printf_core.error_mapper
    libc.hdr.func.free
)
endforeach()
