if(LIBC_MMIX_BUILD_PTHREAD_CLOCKS)
  add_entrypoint_object(pthread_getcpuclockid
    SRCS linux/mmix/pthread_getcpuclockid.cpp
    HDRS pthread_getcpuclockid.h
    DEPENDS libc.include.pthread
      libc.src.__support.threads.linux.mmix.thread_clock)
endif()
if(LIBC_MMIX_BUILD_CONCURRENCY_HINTS)
  foreach(operation getconcurrency setconcurrency)
    add_entrypoint_object(pthread_${operation}
      SRCS linux/mmix/pthread_${operation}.cpp
      HDRS pthread_${operation}.h
      DEPENDS libc.include.pthread
        libc.src.__support.threads.linux.mmix.concurrency_hint)
  endforeach()
endif()
