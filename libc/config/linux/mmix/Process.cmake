option(LIBC_MMIX_BUILD_PROCESS_BASE "Build Linux environment and non-fork process providers" OFF)
if(NOT LIBC_MMIX_BUILD_PROCESS_BASE)
  return()
endif()

set(MMIX_PROCESS_ENTRYPOINTS
  libc.src.unistd.environ
  libc.src.errno.program_invocation_name
  libc.src.errno.program_invocation_short_name
  libc.src.stdlib.getenv
  libc.src.stdlib.setenv
  libc.src.stdlib.unsetenv
  libc.src.stdlib.putenv
  libc.src.unistd.execve
  libc.src.unistd.execv
  libc.src.unistd.setsid
  libc.src.sys.resource.getrlimit
  libc.src.sys.resource.setrlimit
  libc.src.sys.resource.getrusage
  libc.src.sys.wait.waitpid
  libc.src.sys.wait.wait4
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_PROCESS_ENTRYPOINTS})
