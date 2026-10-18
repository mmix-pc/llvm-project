set(system_source mmix/system.cpp)
set(system_platform_deps "")
set(system_extra_deps
  libc.src.__support.CPP.mutex
  libc.src.__support.threads.mutex
  libc.src.signal.sigaction
  libc.src.__support.threads.linux.mmix.process_operation)
if(LIBC_MMIX_BUILD_COORDINATED_FORK)
  list(APPEND system_options -DLIBC_MMIX_SYSTEM_COORDINATED)
  list(APPEND system_extra_deps
    libc.src.__support.threads.linux.mmix.fork_runtime)
endif()
if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
  list(APPEND system_options -DLIBC_MMIX_SYSTEM_CANCELLATION)
  list(APPEND system_extra_deps
    libc.src.__support.threads.linux.mmix.cancellation
    libc.src.__support.threads.linux.mmix.cancellation_point)
endif()
