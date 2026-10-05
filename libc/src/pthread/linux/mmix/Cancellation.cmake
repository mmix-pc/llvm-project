if(NOT LIBC_MMIX_BUILD_DEFERRED_CANCELLATION)
  return()
endif()
foreach(operation cancel setcancelstate setcanceltype testcancel)
  add_entrypoint_object(
    pthread_${operation}
    SRCS linux/mmix/pthread_${operation}.cpp
    HDRS pthread_${operation}.h
    DEPENDS libc.include.pthread
      libc.src.__support.threads.linux.mmix.cancellation
    COMPILE_OPTIONS -fexceptions -funwind-tables
  )
endforeach()

if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
  foreach(operation pthread_join pthread_cond_wait pthread_cond_timedwait
                    pthread_cond_clockwait)
    set(${operation}_source linux/mmix/cancellation_${operation}.cpp)
  endforeach()
  set(mmix_wait_dependencies
    libc.src.__support.threads.linux.mmix.cancellation_point
    libc.src.__support.threads.linux.mmix.cancellation
    libc.src.__support.threads.linux.mmix.thread_join
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.hdr.signal_macros libc.include.sys_syscall)
  set(mmix_wait_options -fexceptions -funwind-tables)
endif()
