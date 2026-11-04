set(thrd_create_source linux/mmix/thrd_create.cpp)
set(thrd_create_dependencies libc.config.app_h
  libc.src.__support.threads.linux.mmix.stack_policy)
set(thrd_detach_source linux/mmix/thrd_detach.cpp)
set(thrd_detach_dependencies libc.src.pthread.pthread_detach)
