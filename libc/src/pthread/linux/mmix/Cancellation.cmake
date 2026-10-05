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
