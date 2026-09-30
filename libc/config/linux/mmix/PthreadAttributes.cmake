option(LIBC_MMIX_BUILD_PTHREAD_ATTRIBUTES
  "Prepare public pthread types and core attributes without thread creation" OFF)
if(NOT LIBC_MMIX_BUILD_PTHREAD_ATTRIBUTES)
  return()
endif()

if(LIBC_MMIX_BUILD_THREAD_LIFECYCLE OR
   LIBC_MMIX_BUILD_COMMAND_ENVIRONMENT_RUNTIME OR LIBC_MMIX_INSTALL_RUNTIME)
  message(FATAL_ERROR
    "MMIX pthread attributes are preparation only, not an installed thread runtime")
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.pthread.pthread_attr_init
  libc.src.pthread.pthread_attr_destroy
  libc.src.pthread.pthread_attr_getdetachstate
  libc.src.pthread.pthread_attr_setdetachstate
  libc.src.pthread.pthread_attr_getguardsize
  libc.src.pthread.pthread_attr_setguardsize
  libc.src.pthread.pthread_attr_getstack
  libc.src.pthread.pthread_attr_setstack
  libc.src.pthread.pthread_attr_getstacksize
  libc.src.pthread.pthread_attr_setstacksize
  libc.src.pthread.pthread_attr_getschedparam
  libc.src.pthread.pthread_attr_setschedparam)
set(TARGET_LLVMLIBC_ENTRYPOINTS ${TARGET_LIBC_ENTRYPOINTS})
