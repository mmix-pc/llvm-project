set(pthread_attr_init_source linux/mmix/pthread_attr_init.cpp)
if(NOT LIBC_MMIX_BUILD_PTHREAD_SCHEDULING)
  return()
endif()
set(mmix_scheduling_dependency libc.src.__support.threads.linux.mmix.thread_scheduling)
foreach(operation pthread_attr_getschedparam pthread_attr_setschedparam pthread_create pthread_setschedparam pthread_getschedparam)
  set(${operation}_source linux/mmix/${operation}.cpp)
endforeach()
foreach(operation pthread_attr_getschedpolicy pthread_attr_setschedpolicy pthread_attr_getinheritsched pthread_attr_setinheritsched pthread_attr_getscope pthread_attr_setscope pthread_setschedprio)
  add_entrypoint_object(${operation}
    SRCS linux/mmix/${operation}.cpp
    HDRS ${operation}.h
    DEPENDS libc.include.pthread
      libc.src.__support.threads.linux.mmix.thread_scheduling)
endforeach()
