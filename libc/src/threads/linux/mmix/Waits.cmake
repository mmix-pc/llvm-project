if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
  set(cnd_wait_source mmix/cnd_wait.cpp)
  set(cnd_timedwait_source mmix/cnd_timedwait.cpp)
  set(cnd_wait_dependencies
    libc.src.pthread.pthread_cond_utils
    libc.src.__support.threads.linux.mmix.cancellation
    libc.src.__support.threads.linux.mmix.cancellation_point)
  set(cnd_wait_options -fexceptions -funwind-tables)
endif()
